#ifndef CLIENT_LIST_H
#define CLIENT_LIST_H

#include <unordered_map>
#include <vector>
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
    void updateLastDebugMsgCtr(int sockfd, uint32_t ctr);
    void updatePingSent(int sockfd, bool sent);
    bool getPingSent(int sockfd);

    void forEachClient(std::function<void(int sockfd, ClientInfo&)> cb);
    void removeStaleClients(int maxAgeSeconds);
    void debugPrintAll(const char* tag = "ClientsList");
    size_t size() const;

    std::vector<ClientInfo*> getSlaves();
    std::vector<ClientInfo*> getBrowsers();

private:
    ClientsList();
    SemaphoreHandle_t mtx_;
    std::unordered_map<int, ClientInfo> clients_;
};
#endif // CLIENT_LIST_H