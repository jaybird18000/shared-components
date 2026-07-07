#include "WsReceiver.h"
#include "wsServer.h"
#include "WsServerMgr.h"
#include "clientsList.h"
#include "esp_log.h"

static const char* TAG = "WsReceiver";

struct ReceiverParams {
    QueueHandle_t queue;
};

void WsReceiver::start(QueueHandle_t incomingQueue) {
    ReceiverParams* params = new ReceiverParams{incomingQueue};

    xTaskCreate(task, "WsReceiverTask", 4096, params, 6, nullptr);
}

void WsReceiver::task(void* param) {
    ReceiverParams* p = (ReceiverParams*)param;

    while (true) {
        IncomingMsg msg;
        if (xQueueReceive(p->queue, &msg, portMAX_DELAY)) {
            int sockfd = msg.sockfd;
            bool isSlave = msg.isSlave;
            const char* role = isSlave ? "SLAVE" : "BROWSER";
            switch (msg.type) {
            case HTTPD_WS_TYPE_PONG:


                ClientsList::instance().updateLastSeen(sockfd);
                ClientsList::instance().updatePingSent(sockfd, false);
//                ESP_LOGI(TAG, "PONG rcvd fd=%d from %s", sockfd, role);
                break;

            case HTTPD_WS_TYPE_TEXT:
            case HTTPD_WS_TYPE_BINARY:
                {
                ClientsList::instance().updateLastSeen(sockfd);
                std::string payload(msg.data, msg.len);  // local, safe
//                ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", sockfd, role, payload.c_str());                
                if(isSlave)
                {
                    WsServerMgr::enqueueSlaveClient(sockfd, payload);
                }
                else
                {
                    WsServerMgr::enqueueWebClient(sockfd, payload);
                };
                
                break;
            }
            case HTTPD_WS_TYPE_PING:
                // Optional: auto-respond to PING if you want
                // WsServer::instance().sendPongMsg(sockfd);
                ESP_LOGI(TAG, "PING rcvd fd=%d from ", sockfd, role);
                break;

            default:

                break;
            }
        }
    }
}