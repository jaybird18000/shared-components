#ifndef WS_SERVER_MGR_H
#define WS_SERVER_MGR_H

#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class WsServerMgr {
public:
    static void start();
    static void enqueueWebClient(int sockfd, const std::string& payload);
    static void enqueueSlaveClient(int sockfd, const std::string& payload);

private:
    static void webClientWorkerTask(void* param);
    static void slaveClientWorkerTask(void* param);
    static void pingTask(void* param);
    static void AcUpdateTask(void* param);
    static void broadcastStatusTask(void* param);
    static void handleWebClientMsg(int sockfd, const std::string& message);
    static void handleSlaveClientMsg(int sockfd, const std::string& message);
    static void broadcastStatus();
    struct WorkItem {
        int sockfd;
        uint16_t len;
        char data[256];   // tune size as needed
    };

    static QueueHandle_t webClientQueue_;
    static QueueHandle_t slaveClientQueue_;
    static QueueHandle_t g_acUpdateQueue;
};

#endif // WS_SERVER_MGR_H