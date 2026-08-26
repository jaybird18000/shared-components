#include "ClientsList.h"
#include "ConsoleApp.h"
#include "esp_log.h"
#include <chrono>

ClientsList::ClientsList() {
    mtx_ = xSemaphoreCreateRecursiveMutex();
}

ClientsList& ClientsList::instance() {
    static ClientsList inst;
    return inst;
}

bool ClientsList::addClient(int sockfd, const ClientInfo& info) {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    clients_[sockfd] = info;
    xSemaphoreGiveRecursive(mtx_);
    return true;
}

bool ClientsList::removeClient(int sockfd) {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    bool removed = clients_.erase(sockfd) > 0;
    xSemaphoreGiveRecursive(mtx_);
    return removed;
}

ClientInfo* ClientsList::findClient(int sockfd) {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    auto it = clients_.find(sockfd);
    ClientInfo* result = (it != clients_.end()) ? &it->second : nullptr;
    xSemaphoreGiveRecursive(mtx_);
    return result;
}

void ClientsList::updateLastSeen(int sockfd) {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    auto it = clients_.find(sockfd);
    if (it != clients_.end()) {
        it->second.lastSeen = std::chrono::steady_clock::now();
    }
    xSemaphoreGiveRecursive(mtx_);
}
void ClientsList::updateLastPongRxTime(int sockfd) {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    auto it = clients_.find(sockfd);
    if (it != clients_.end()) {
        it->second.lastPongRxTime = std::chrono::steady_clock::now();
    }
    xSemaphoreGiveRecursive(mtx_);
}

void ClientsList::updateLastDebugMsgCtr(int sockfd, uint32_t ctr)
{
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    auto it = clients_.find(sockfd);
    if (it != clients_.end()) {
        it->second.lastSentDebugMsgCtr = ctr;
    }
    xSemaphoreGiveRecursive(mtx_);
}

void ClientsList::updatePingSent(int sockfd, bool sent) {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    auto it = clients_.find(sockfd);
    if (it != clients_.end()) {
        it->second.pingSent = sent;
    }
    xSemaphoreGiveRecursive(mtx_);
}

void ClientsList::updateClientType(int sockfd, ClientInfo::ClientType type)
{
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    auto it = clients_.find(sockfd);
    if (it != clients_.end()) {
        it->second.type = type;
    }
    xSemaphoreGiveRecursive(mtx_);
}

bool ClientsList::getPingSent(int sockfd) {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    auto it = clients_.find(sockfd);
    bool result = (it != clients_.end()) ? it->second.pingSent : false;
    xSemaphoreGiveRecursive(mtx_);
    return result;
}

void ClientsList::forEachClient(std::function<void(int, ClientInfo&)> cb) {
    // Take snapshot of keys
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    std::vector<int> keys;
    keys.reserve(clients_.size());
    for (auto& kv : clients_) {
        keys.push_back(kv.first);
    }
    xSemaphoreGiveRecursive(mtx_);

    // Iterate snapshot
    for (int fd : keys) {
        xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
        auto it = clients_.find(fd);
        if (it != clients_.end()) {
            cb(fd, it->second);
        }
        xSemaphoreGiveRecursive(mtx_);
    }
}

void ClientsList::removeStaleClients(int maxAgeSeconds) {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    auto now = std::chrono::steady_clock::now();

    for (auto it = clients_.begin(); it != clients_.end(); ) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.lastSeen
        ).count();

        if (age > maxAgeSeconds) {
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
    xSemaphoreGiveRecursive(mtx_);
}

size_t ClientsList::size() const {
    // size() must also lock
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    size_t s = clients_.size();
    xSemaphoreGiveRecursive(mtx_);
    return s;
}

std::vector<ClientInfo*> ClientsList::getSlaves() {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    std::vector<ClientInfo*> out;
    for (auto& [fd, info] : clients_) {
        if (info.type == ClientInfo::ClientType::SLAVE) out.push_back(&info);
    }
    xSemaphoreGiveRecursive(mtx_);
    return out;
}

std::vector<ClientInfo*> ClientsList::getBrowsers() {
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    std::vector<ClientInfo*> out;
    for (auto& [fd, info] : clients_) {
        if (info.type == ClientInfo::ClientType::BROWSER) out.push_back(&info);
    }
    xSemaphoreGiveRecursive(mtx_);
    return out;
}

std::vector<ClientInfo *> ClientsList::getSunshades()
{
    xSemaphoreTakeRecursive(mtx_, portMAX_DELAY);
    std::vector<ClientInfo*> out;
    for (auto& [fd, info] : clients_) {
        if (info.type == ClientInfo::ClientType::SUNSHADE) out.push_back(&info);
    }
    xSemaphoreGiveRecursive(mtx_);
    return out;
}

void ClientsList::debugPrintAll(const char* tag, bool toLog, bool toConsole)
{
    // Lock the mutex safely
    xSemaphoreTake(mtx_, portMAX_DELAY);

    char header[64];
    snprintf(header, sizeof(header), "----- ClientsList (%u clients) -----", (unsigned)clients_.size());
    if (toLog) ESP_LOGW(tag, "%s", header);
    if (toConsole) ConsoleApp::instance().writeToConsole(header);

    auto now = std::chrono::steady_clock::now();

    for (auto& kv : clients_) {
        int fd = kv.first;
        const ClientInfo& info = kv.second;

        int ageSec = (int)std::chrono::duration_cast<std::chrono::seconds>(
            now - info.lastSeen
        ).count();

        char line[128];
        snprintf(line, sizeof(line),
                 "fd=%-4d id=%-7s type=%-9s pingSent=%-3d age=%-6ds",
                 fd,
                 info.id.c_str(),
                 ClientInfo::typeToString(info.type),
                 info.pingSent,
                 ageSec);

        if (toLog) ESP_LOGW(tag, "%s", line);
        if (toConsole) ConsoleApp::instance().writeToConsole(line);
    }

    const char* footer = "--------------------------------------";
    if (toLog) ESP_LOGW(tag, "%s", footer);
    if (toConsole) ConsoleApp::instance().writeToConsole(footer);

    xSemaphoreGive(mtx_);
}

void ClientsList::debugPrintCounters(const char* tag, bool toLog, bool toConsole)
{
    // Lock the mutex safely
    xSemaphoreTake(mtx_, portMAX_DELAY);

    char header[64];
    snprintf(header, sizeof(header), "----- ClientsList Counters (%u clients) -----", (unsigned)clients_.size());
    if (toLog) ESP_LOGW(tag, "%s", header);
    if (toConsole) ConsoleApp::instance().writeToConsole(header);

    for (auto& kv : clients_) {
        int fd = kv.first;
        const ClientInfo& info = kv.second;

        char line[160];
        snprintf(line, sizeof(line),
                 "fd=%-4d type=%-9s lastSentDebugMsgCtr=%-6lu lastSentMasterDebugMsgCtr=%-6lu webClientLastReceivedDebugMsgCtr=%-6lu debugMessagesInSync=%-3d",
                 fd,
                 ClientInfo::typeToString(info.type),
                 info.lastSentDebugMsgCtr,
                 info.getLastSentMasterDebugMsgCtr(),
                 info.webClientLastReceivedDebugMsgCtr,
                 info.debugMessagesInSync);

        if (toLog) ESP_LOGW(tag, "%s", line);
        if (toConsole) ConsoleApp::instance().writeToConsole(line);
    }

    const char* footer = "--------------------------------------";
    if (toLog) ESP_LOGW(tag, "%s", footer);
    if (toConsole) ConsoleApp::instance().writeToConsole(footer);

    xSemaphoreGive(mtx_);
}