#include "r33ClientController.h"
#include "DeviceConfig.h"
#include "wifiMgr.h"
#include "WsServer.h"
#include "Utilities.h"
#include "SharedDataStore.h"
#include "TaskUtilities.h"
#include "DebugServices.h"
#include "arduinojson.h"
#include "esp_log.h"

//static ACMonitor acMonitor;
//static GenRelay genRelay;

R33ClientController& R33ClientController::instance() {
    static R33ClientController inst;
    return inst;
}

void R33ClientController::startMessageTask() {
    if (messageTaskHandle)
    {
        ESP_LOGW(TAG, "Client controller messsage task already running");
        return;
    }

    BaseType_t ok2 = TaskUtilities::createTaskWithQueue(
        TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK,
        &R33ClientController::messageTaskEntry,
        sizeof(TaskUtilities::MsgItem),
        this
    );
    if (ok2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create client controller message task");
        messageTaskHandle= nullptr;
    } 
    else 
    {
        ESP_LOGI(TAG, "Client controller task started");
    }

}
void R33ClientController::startSocketTask() {
    if (socketTaskHandle)
    {
        ESP_LOGW(TAG, "Client controller socket task already running");
        return;
    }

    if (!wsEvents) {
        wsEvents = xEventGroupCreate();
        if (!wsEvents) {
            ESP_LOGE(TAG, "Failed to create event group");
            return;
        }
    }

    // callback from WebSocket client (runs in WS task context)
    r33Client.setClientDisconnectedCallback(
        [this](int sockfd) {
            this->onClientDisconnected(sockfd);
        }
    );


    //BaseType_t ok1 = xTaskCreate(
    //    &R33ClientController::socketTaskEntry,
    //    "r33_slave_ctrl",
    //    8192,
    //    this,
    //    6,
    //    &socketTaskHandle
    //);

    BaseType_t ok1 = TaskUtilities::createTaskWithQueue(
        TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_SOCKET_TASK,
        &R33ClientController::socketTaskEntry,
        sizeof(TaskUtilities::MsgItem),
        this
    );
    

    

    if (ok1 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create client controller socket task");
        socketTaskHandle = nullptr;
    }
    else 
    {
        ESP_LOGI(TAG, "Client controller socket task started");
    }

}

void R33ClientController::socketTaskEntry(void* param) {
    auto* self = static_cast<R33ClientController*>(param);
    self->socketTaskLoop();
}

void R33ClientController::messageTaskEntry(void* param) {
    auto* self = static_cast<R33ClientController*>(param);
    self->messageTaskLoop();
}

void R33ClientController::socketTaskLoop() {
    ESP_LOGI(TAG, "Client controller socket loop running");

    masterHost.clear();
    masterIp.clear();
    masterPort             = 80;
    lastReconnectAttemptMs = 0;

    while (true) 
    {
//        ESP_LOGW(TAG, "loop tick");
        // do not start processing until wifi is stable
        if(WifiMgr::instance().mdnsStarted())
        {
            // ---------------------------------------------------------
            // 0. Handle deferred disconnect events (from callback)
            // ---------------------------------------------------------
            EventBits_t bits = xEventGroupWaitBits(
                wsEvents,
                WS_EVENT_DISCONNECTED,
                pdTRUE,     // clear on exit
                pdFALSE,
                0           // non-blocking
            );

            if (bits & WS_EVENT_DISCONNECTED) {
                ESP_LOGW(TAG, "Controller: disconnect event received, stopping client");
                r33Client.stop();   // SAFE here (not in WS callback)
                // small delay to let WS client fully shut down
                vTaskDelay(pdMS_TO_TICKS(2000));
            }

            // ---------------------------------------------------------
            // 1. If not connected, attempt reconnect logic
            // ---------------------------------------------------------
            if (!r33Client.isConnected())
            {
                uint32_t now = esp_log_timestamp();
                if ((now - lastReconnectAttemptMs) >= RECONNECT_DELAY_MS) 
                {
                    uint32_t diff = now - lastReconnectAttemptMs;
                    ESP_LOGW(TAG, "Client not connected — attempting to connect, diff %d lastRecon %d", diff , lastReconnectAttemptMs);
                    lastReconnectAttemptMs = now;
                    // Safety stop before new begin, if we started but no connected or disconnected event fired
                    // we need to stop the current client so we can start a new one
                    if(r33Client.isStarted())
                    {
                        ESP_LOGW(TAG, "Stopping client that never started properly");
                        r33Client.stop();  // internally should call esp_websocket_client_stop()
                        vTaskDelay(pdMS_TO_TICKS(2000));  // give the stack time to fully close
                        ESP_LOGW(TAG, "Finished Stopping client that never started properly");
                    }

                    // 1A. Discover master via mDNS
                    bool foundMaster = discoverMaster(masterHost, masterIp, masterPort);
                    if (foundMaster) 
                    {
                        if (!masterHost.ends_with(".local")) {
                            masterHost += ".local";
                        }

                        ESP_LOGI(TAG, "Master discovered: %s:%d",
                                masterHost.c_str(), masterPort);

                        bool ping_status = Utilities::ping_addr(masterIp.c_str());
                        ping_status = Utilities::ping_addr(masterIp.c_str());
                        if(!ping_status)
                        {
                            ESP_LOGW(TAG, "Ping to master %s failed, attempting to reset network stack", masterIp.c_str());
                            ESP_LOGW(TAG, "Network stack stuck — resetting netif");
                            WifiMgr::instance().restartWifi();  // this take a while
                            ping_status = Utilities::ping_addr(masterIp.c_str());
                        }
                        vTaskDelay(pdMS_TO_TICKS(2000));  // give tmdns time to settle
                        // ⭐ Wait for master’s port 80 to be open
                        bool ready = false;
                        for (int i = 0; i < 5; i++) {   // 20 × 250ms = 5 seconds max
                            if (isPortOpen(masterIp, masterPort, 1500)) {
                                ready = true;
                                break;
                            }
                            vTaskDelay(pdMS_TO_TICKS(250));
                        }

                        if (!ready) {
                            ESP_LOGW(TAG, "Master found but port %d is not open yet, retry later", masterPort);
                            vTaskDelay(pdMS_TO_TICKS(2000));
                            continue;   // or retry later
                        }
                        else
                        {

                        }                      
                        ESP_LOGI(TAG, "Master port is open, Attempting to connect to %s:%d", masterHost.c_str(), masterPort);
                        // update the lastReconnectAttempt since it could have taken a long
                        // time to get here
                        lastReconnectAttemptMs = esp_log_timestamp();
                        r33Client.begin(masterIp.c_str(), masterPort, "/slave");
//                        client.begin(masterHost.c_str(), masterPort, "/slave");
                    } 
                    else 
                    {
                        ESP_LOGW(TAG, "Master not found via mDNS");
                    }
                } 
                else
                {
                    ESP_LOGI(TAG, "Client not connected, waiting for reconnect window");
                }
            } 
            else 
            {
    //            ESP_LOGI(TAG, "Client is connected");
            }

        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // 2-second interval
    } // while(true)

    // we should never get here
    ESP_LOGW(TAG, "Exiting Client controller task loop");
}

void R33ClientController::messageTaskLoop() {
    ESP_LOGI(TAG, "Client controller message loop running");
    while (true)
    {
        TaskUtilities::MsgItem item{};

        if(TaskUtilities::waitOnQueue(TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK, &item, portMAX_DELAY) == pdTRUE) 
        {

            switch (item.msgType)
            {
                case TaskUtilities::MsgTypes::DEBUG:
                {
                    item.fromTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
                    item.toTask = TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK;
                    //ESP_LOGW(TAG, "Processing DEBUG message: %s", item.data);
                    auto bytes = item.serialize();
                    item.len = bytes.size();
                    clientSendBinary(bytes.data(), bytes.size());
                    break;
                }
                case TaskUtilities::MsgTypes::REGISTRATION:
                {
                    item.fromTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
                    item.toTask = TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK;
                    ESP_LOGW(TAG, "Processing REGISTRATION message: %s", item.data);
                    // create a message to send to master
                    DynamicJsonDocument doc(256);

                    doc["type"] = "clientRegistration";
                    if(DeviceConfig::instance().isSlaveDevice())
                    {
                        doc["source"] = "slave";
                    }
                    else
                    {
                        doc["source"] = "sunshade";
                    }

                    std::string out;
                    serializeJson(doc, out);  
                    item.len = out.size(); 
                    item.setMsg(out); 
                    auto bytes = item.serialize();  
                    ESP_LOGI(TAG,"Sending client registration message to master: %s", out.c_str());           
                    clientSendBinary(bytes.data(), bytes.size());

                    debugServices::postDebug("Connected to master, sending registration msg");
                    break;
                }
                case TaskUtilities::MsgTypes::DATA:
                    ESP_LOGW(TAG, "Processing DATA message: %s", item.data);
                    if(DeviceConfig::instance().isSlaveDevice()) 
                    {
                        if(item.toTask != TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK)
                        {
                            ESP_LOGW(TAG, "Received DATA message not intended for R33_SLAVE_CONTROLLER_MESSAGE_TASK: %s", item.data);
                        }
                        item.fromTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
                        item.toTask = TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK;
                        TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK, &item, portMAX_DELAY);
                        ESP_LOGW(TAG, "Received JSON message send to R33_SLAVE_CONTROLLER_TASK: %s", item.data);
                    }
                    else if(DeviceConfig::instance().isSunShadeDevice()) 
                    {
                        if(item.toTask != TaskUtilities::TaskNames::R33_SUNSHADE_CONTROLLER_MESSAGE_TASK)
                        {
                            ESP_LOGW(TAG, "Received DATA message not intended for R33_SUNSHADE_CONTROLLER_MESSAGE_TASK: %s", item.data);
                        }
                        item.fromTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
                        item.toTask = TaskUtilities::TaskNames::R33_SUNSHADE_CONTROLLER_MESSAGE_TASK;
                        TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_SUNSHADE_CONTROLLER_MESSAGE_TASK, &item, portMAX_DELAY);
                        ESP_LOGW(TAG, "Received JSON message send to R33_SUNSHADE_CONTROLLER_TASK: %s", item.data);
                    }
                    else {
                        ESP_LOGW(TAG, "Received message in client controller message loop but this is neither sunshade nor slave device, ignoring: %s", item.data);
                    }
                    break;
                default:
                    ESP_LOGW(TAG, "Did not handle msgType %d", item.msgType);

            }
        }

    }
}

bool R33ClientController::processJsonDebugMessage(const char* json) {
    DynamicJsonDocument doc(256);
    auto err = deserializeJson(doc, json);

    if (err) 
    {
        ESP_LOGW(TAG, "Invalid JSON from ???: %s", json);
        ESP_LOGW(TAG, "First 20 chars: %.*s", 20, json);
        return false;
    }

    const char* type = doc["type"];
    if(type != nullptr) 
    {
        if(strcmp(type, "debug") == 0)
        {
            // highjack the debug messages and send to master
            int id = doc["id"];
            std::string msg = doc["message"];
    //        ESP_LOGI(TAG, "r33ClientController Rcvd DEBUG json message, forward to master %s", json);
            //clientSend(json);
            return true;
        }
    }
    else
    {
//        ESP_LOGI(TAG, "r33ClientControllerRcvd json message, pass upstream: %s", json);

    }

    return false;
}
bool R33ClientController::isClientConnected() {
    return r33Client.isConnected();
}

bool R33ClientController::clientSend(const std::string& msg) {
    return r33Client.send(msg);
}
bool R33ClientController::clientSendBinary(const uint8_t *data, size_t len)
{
    return r33Client.sendBinary(data, len);
}

bool R33ClientController::discoverMaster(std::string& hostname, std::string& ip, int& port) {
    mdns_result_t* result = nullptr;
    esp_err_t err = mdns_query_ptr("_http", "_tcp", 3000, 20, &result);
    if (err != ESP_OK || !result) {
        ESP_LOGW(TAG, "mdns_query_ptr error %s (empty=%d)",
                 esp_err_to_name(err), result == nullptr);
        return false;
    }

    mdns_result_t* r = result;
    while (r) {
        if (r->hostname && strcmp(r->hostname, "r33-master") == 0) {

            hostname = r->hostname;
            port     = r->port;

            // ⭐ Extract IPv4 address
            if (r->addr && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
                char ipStr[16] = {0};
                inet_ntoa_r(r->addr->addr.u_addr.ip4, ipStr, sizeof(ipStr));
                ip = ipStr;
            } else {
                ESP_LOGW(TAG, "Master found but no IPv4 address in mDNS record");
                mdns_query_results_free(result);
                return false;
            }

            ESP_LOGI(TAG, "Master discovered: %s (%s:%d)",
                     hostname.c_str(), ip.c_str(), port);

            mdns_query_results_free(result);
            return true;
        }
        r = r->next;
    }

    mdns_query_results_free(result);
    return false;
}

bool R33ClientController::isPortOpen(const std::string& ip, int port, int timeoutMs)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    // Non-blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);

    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (res < 0 && errno != EINPROGRESS) {
        close(sock);
        return false;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);

    struct timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    res = select(sock + 1, NULL, &wfds, NULL, &tv);

    if (res <= 0) {
        close(sock);
        return false;
    }

    // MUST check SO_ERROR
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);

    close(sock);
    return (err == 0);
}

void R33ClientController::onClientDisconnected(int sockfd) {
    ESP_LOGW(TAG, "onClientDisconnected called, sockfd=%d", sockfd);
    if (wsEvents) {
        xEventGroupSetBits(wsEvents, WS_EVENT_DISCONNECTED);
    }
}