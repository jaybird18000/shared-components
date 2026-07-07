#include "ClientsList.h"
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

void ClientsList::debugPrintAll(const char* tag)
{
    // Lock the mutex safely
    xSemaphoreTake(mtx_, portMAX_DELAY);

    ESP_LOGW(tag, "----- ClientsList (%u clients) -----", clients_.size());

    auto now = std::chrono::steady_clock::now();

    for (auto& kv : clients_) {
        int fd = kv.first;
        const ClientInfo& info = kv.second;

        int ageSec = (int)std::chrono::duration_cast<std::chrono::seconds>(
            now - info.lastSeen
        ).count();

        ESP_LOGW(tag,
                 "fd=%d  id=%s  type=%s  pingSent=%d  age=%ds",
                 fd,
                 info.id.c_str(),
                 ClientInfo::typeToString(info.type),
                 info.pingSent,
                 ageSec);
    }

    ESP_LOGW(tag, "--------------------------------------");

    xSemaphoreGive(mtx_);
}