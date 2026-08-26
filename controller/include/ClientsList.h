#ifndef CLIENT_LIST_H
#define CLIENT_LIST_H

#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include "ClientInfo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class ClientsList {
public:
    static ClientsList& instance();

    bool addClient(int sockfd, const ClientInfo& info);
    bool removeClient(int sockfd);
    ClientInfo* findClient(int sockfd);

    void updateLastSeen(int sockfd);
    void updateLastPongRxTime(int sockfd);
    void updateLastDebugMsgCtr(int sockfd, uint32_t ctr);
    void updatePingSent(int sockfd, bool sent);
    void updateClientType(int sockfd, ClientInfo::ClientType type);
    bool getPingSent(int sockfd);

    void forEachClient(std::function<void(int sockfd, ClientInfo&)> cb);
    void removeStaleClients(int maxAgeSeconds);
    void debugPrintAll(const char* tag = "ClientsList", bool toLog = true, bool toConsole = false);
    void debugPrintCounters(const char* tag = "ClientsList", bool toLog = true, bool toConsole = false);

    size_t size() const;

    std::vector<ClientInfo*> getSlaves();
    std::vector<ClientInfo*> getBrowsers();
    std::vector<ClientInfo*> getSunshades();

private:
    ClientsList();
    SemaphoreHandle_t mtx_;
    std::unordered_map<int, ClientInfo> clients_;
};
#endif // CLIENT_LIST_H