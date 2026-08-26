#include "wsServerMgr.h"
#include "WsServer.h"
#include "r33MasterController.h"
#include "clientsList.h"
#include "GenRelay.h"
#include "nvsMgr.h"
#include "SharedDataStore.h"
#include "DebugServices.h"
#include "DeviceConfig.h"
#include "TaskUtilities.h"
#include "ArduinoJson.h"
#include "AcUpdateTask.h"
#include "Valves.h"
#include "LedController.h"
#include "OtaManager.h"
#include "version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "String.h"
#include "cstring"
#include "cmath"

static const char* TAG = "wsServerMgr";

wsServerMgr& wsServerMgr::instance() {
    static wsServerMgr inst;
    return inst;
}

void wsServerMgr::start() {

    TaskUtilities::createTaskWithQueue(TaskUtilities::TaskNames::WS_SERVER_MGR_TASK, wsServerMgrTask, sizeof(TaskUtilities::MsgItem));

    TaskUtilities::createTask(TaskUtilities::TaskNames::WS_SERVER_MGR_PING_TASK, wsServerMgrPingTask);

}

void wsServerMgr::wsServerMgrTask(void* param) {
    while (true) {
        TaskUtilities::MsgItem item;

        if (TaskUtilities::waitOnQueue(TaskUtilities::TaskNames::WS_SERVER_MGR_TASK, &item, portMAX_DELAY) == pdTRUE) {
//            ESP_LOGI(TAG, "Processing web payload from fd=%d: %s",item.sockfd, item.data);
            switch (item.frameType) {
            case HTTPD_WS_TYPE_PONG:
            {
                const char* role = item.fromClient ? "SLAVE" : "BROWSER";
                ClientsList::instance().updateLastSeen(item.sockfd);
                ClientsList::instance().updatePingSent(item.sockfd, false);
                ClientsList::instance().updateLastPongRxTime(item.sockfd);
//                ESP_LOGI(TAG, "PONG rcvd fd=%d from %s", item.sockfd, role);
                break;
            }

            case HTTPD_WS_TYPE_TEXT:
            case HTTPD_WS_TYPE_BINARY:
                
                ClientsList::instance().updateLastSeen(item.sockfd);
//                ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());  
                item.fromTask = TaskUtilities::TaskNames::WS_SERVER_MGR_TASK;
                if(DeviceConfig::instance().isMasterDevice())
                {
                    item.toTask = TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK;
                    TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK, &item, 0);
                }
                else 
                {
                    item.toTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
                    TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK, &item, 0);
                }

                break;
            
            case HTTPD_WS_TYPE_PING:
                // Optional: auto-respond to PING if you want
                // WsServer::instance().sendPongMsg(sockfd);
                ESP_LOGI(TAG, "PING rcvd fd=%d from x  wsServerMgr should not rcv PING", item.sockfd);
                break;

            default:

                break;
            }
 
            // TODO: your heavy logic here
        }
    }
}

void wsServerMgr::wsServerMgrPingTask(void* param) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        auto& list = ClientsList::instance();

        list.forEachClient([&](int fd, ClientInfo& info) {
            if(info.type != ClientInfo::ClientType::MASTER)
            {
                auto now = std::chrono::steady_clock::now();

                auto diffLastSeenMs = 
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - info.lastSeen).count();
                auto diffLastPongRxTimeMs = 
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - info.lastPongRxTime).count();

                if (!info.pingSent) {
                    WsServer::instance().sendPingMsg(fd);
                    list.updatePingSent(fd, true);
//                    ESP_LOGI(TAG,"sent ping to fd %d - diffMs %lld", fd, diffMs);
                }
                else if (diffLastSeenMs > 3000) {
                    ESP_LOGW(TAG, "Client fd=%d timed out, diff %lldms, closing ", fd, diffLastSeenMs);
                    httpd_sess_trigger_close(WsServer::instance().serverHandle(), fd);
                    list.removeClient(fd);
                }
                else if (diffLastPongRxTimeMs > 3000) {
                    // reset ping sent so we will send another ping, at this point there is still active traffic 
                    // but for some reason the pong never was received. 
                    ESP_LOGW(TAG, "Client fd=%d has not responded to ping, diff %lldms reset pingSent", fd, diffLastPongRxTimeMs);
                    list.updatePingSent(fd, false);
                }
            }
        });
    }
}