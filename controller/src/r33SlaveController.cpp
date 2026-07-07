#include "r33SlaveController.h"
#include "ACMonitor.h"
#include "GenRelay.h"
#include "wifiMgr.h"
#include "WsServer.h"
#include "SharedDataStore.h"
#include "arduinojson.h"
#include "esp_log.h"

static ACMonitor acMonitor;
static GenRelay genRelay;

R33SlaveController& R33SlaveController::instance() {
    static R33SlaveController inst;
    return inst;
}

void R33SlaveController::begin() {
    if ((socketTaskHandle) || (messageTaskHandle)  || (measurementTaskHandle))
    {
        ESP_LOGW(TAG, "Slave controller already running");
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
    client.setClientDisconnectedCallback(
        [this](int sockfd) {
            this->onClientDisconnected(sockfd);
        }
    );
    commandQueue = xQueueCreate(10, sizeof(char*));
    client.setCommandQueue(commandQueue);
    WsServer::instance().setSlaveControllerCommandQueue(commandQueue);

    BaseType_t ok1 = xTaskCreate(
        &R33SlaveController::socketTaskEntry,
        "r33_slave_ctrl",
        8192,
        this,
        6,
        &socketTaskHandle
    );

    BaseType_t ok2 = xTaskCreate(
        &R33SlaveController::messageTaskEntry,
        "r33_slave_message_ctrl",
        8192,
        this,
        5,
        &messageTaskHandle
    );
    
    BaseType_t ok3 = xTaskCreate(
        &R33SlaveController::measurementTaskEntry,
        "r33_slave_measurement_ctrl",
        8192,
        this,
        5,
        &measurementTaskHandle
    );

    if (ok1 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create slave controller socket task");
        socketTaskHandle = nullptr;
    }
    else if (ok2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create slave controller message task");
        messageTaskHandle= nullptr;
    } 
    else if (ok3 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create slave controller measurement task");
        measurementTaskHandle = nullptr;
    } 
    else 
    {
        ESP_LOGI(TAG, "Slave controller task started");
    }

    acMonitor.init();

    genRelay.init();
}

void R33SlaveController::socketTaskEntry(void* param) {
    auto* self = static_cast<R33SlaveController*>(param);
    self->socketTaskLoop();
}

void R33SlaveController::messageTaskEntry(void* param) {
    auto* self = static_cast<R33SlaveController*>(param);
    self->messageTaskLoop();
}

void R33SlaveController::measurementTaskEntry(void* param) {
    auto* self = static_cast<R33SlaveController*>(param);
    self->measurementTaskLoop();
}

void R33SlaveController::socketTaskLoop() {
    ESP_LOGI(TAG, "Slave controller socket loop running");

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
                client.stop();   // SAFE here (not in WS callback)
                // small delay to let WS client fully shut down
                vTaskDelay(pdMS_TO_TICKS(2000));
            }

            // ---------------------------------------------------------
            // 1. If not connected, attempt reconnect logic
            // ---------------------------------------------------------
            if (!client.isConnected())
             {
                uint32_t now = esp_log_timestamp();
                if ((now - lastReconnectAttemptMs) >= RECONNECT_DELAY_MS) 
                {
                    uint32_t diff = now - lastReconnectAttemptMs;
                    ESP_LOGW(TAG, "Client not connected — attempting to connect, diff %d lastRecon %d", diff , lastReconnectAttemptMs);
                    lastReconnectAttemptMs = now;
                    // Safety stop before new begin, if we started but no connected or disconnected event fired
                    // we need to stop the current client so we can start a new one
                    if(client.isStarted())
                    {
                        ESP_LOGW(TAG, "Stopping client that never started properly");
                        client.stop();  // internally should call esp_websocket_client_stop()
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
                        client.begin(masterIp.c_str(), masterPort, "/slave");
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
    ESP_LOGW(TAG, "Exiting slave controller task loop");
}

void R33SlaveController::messageTaskLoop() {
    ESP_LOGI(TAG, "Slave controller message loop running");
    while (true) {
        char* jsonMsg = nullptr;

        if (xQueueReceive(commandQueue, &jsonMsg, portMAX_DELAY) == pdTRUE) {
            processJsonCommand(jsonMsg);
            free(jsonMsg);
        }

    }
}

void R33SlaveController::measurementTaskLoop() {
    ESP_LOGI(TAG, "Slave controller measurement loop running");
    while (true) {

//        static int numSent = 0;
//        if(numSent < 10)
        if(true)
        {
//            numSent++;
            std::string acJson = collectACData();
            if (client.isConnected()) {
                bool ok = client.send(acJson);
            }

        }
        else
        {
            ESP_LOGW(TAG, "NOT sending ACDATA to master");
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // 2-second interval
    }
}

void R33SlaveController::processJsonCommand(const char* json) {
    DynamicJsonDocument doc(256);
    auto err = deserializeJson(doc, json);

    if (err) {
        ESP_LOGW(TAG, "Invalid JSON from master: %s", json);
        ESP_LOGW(TAG, "First 20 chars: %.*s", 20, json);
        return;
    }

    const char* type = doc["type"];
    const char* action = doc["payload"]["action"];

    if (strcmp(type, "command") == 0) {
        if (strcmp(action, "start_generator") == 0) {
            startGenerator();
        }
        else if (strcmp(action, "stop_generator") == 0) {
            stopGenerator();
        }
        else 
        {
            ESP_LOGW(TAG, "Unknown JSON command action from master: %s", action);
        }
    }
    else if(strcmp(type, "debug") == 0) {
        int id = doc["id"];
        std::string msg = doc["message"];
//        ESP_LOGI(TAG, "Rcvd Slave DEBUG json message to forward to master%s", json);
        client.send(json);
    }
}

void R33SlaveController::startGenerator()
{
    ESP_LOGI(TAG, "startGenerator called:");
    WsServer::instance().postDebug("Slave: startGenerator called");
    genRelay.momentaryStart();
}

void R33SlaveController::stopGenerator()
{
    ESP_LOGI(TAG, "stopGenerator called:");
    WsServer::instance().postDebug("Slave: stopGenerator called");
    genRelay.momentaryStop();
    
}

bool R33SlaveController::discoverMaster(std::string& hostname, std::string& ip, int& port) {
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

bool R33SlaveController::isPortOpen(const std::string& ip, int port, int timeoutMs)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    // Non-blocking connect
    fcntl(sock, F_SETFL, O_NONBLOCK);

    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (res < 0) {
        if (errno != EINPROGRESS) {
            close(sock);
            return false;
        }
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);

    struct timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    res = select(sock + 1, NULL, &wfds, NULL, &tv);
    close(sock);

    return (res > 0);
}

#if 0
std::string R33SlaveController::collectACData() {
    // TODO: Replace with real AC monitor readings
    float voltage = 120.5f;
    float current = 5.2f;
    uint32_t r = esp_random() % 10;
    voltage = voltage + r - 5;
    r = esp_random() % 2;
    current = current +r - 1;
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"acStatus\",\"voltage\":%.2f,\"current\":%.2f}",
             voltage, current);

    return std::string(buf);
}
#else
std::string R33SlaveController::collectACData() {
    // TODO: Replace with real AC monitor readings
        // Read current values
    ACMonitor::ACResults voltageResults = acMonitor.readVoltage();
    ACMonitor::ACResults currentResults = acMonitor.readCurrent();
    float voltage = voltageResults.rms_voltage;
    float current = currentResults.rms_current;

    genRelay.setState(voltage);

//    ESP_LOGI(TAG, "Collected voltage %0.2f  current %0.2f", voltage, current);
    std::string genStatus = genRelay.stateText();
    // float voltage = 120.5f;
    // float current = 5.2f;
    float freq = voltageResults.frequency;
//    ESP_LOGI(TAG,"freq = %0.1f",freq);
    float watts = 10.0f;
    // collectACData runs on the clinet(slave) side,
    // storing the acData in the SharedDataStore 
    // allows the slave web browser page to show the latest AC data even if the master is not connected, 
    // by reading from the SharedDataStore. The SharedDataStore is updated with the latest AC data every time collectACData is called, 
    // which is every 2 seconds in the measurementTaskLoop. 
    // This way, the slave can display real-time AC data on its web page regardless of the master's connection status.
    SharedDataStore::set(voltage, current, freq, watts, genStatus.c_str());
    // uint32_t r = esp_random() % 10;
    // voltage = voltage + r - 5;
    // r = esp_random() % 2;
    // current = current +r - 1;
    DynamicJsonDocument doc(256);

    doc["type"] = "ac_update";
    doc["source"] = "slave";

    JsonObject p = doc["payload"].to<JsonObject>();
    p["voltage"] = voltage;
    p["current"] = current;
    p["frequency"] = freq;
    p["watts"] = watts;
    p["genStatus"] = genStatus;

    std::string out;
    serializeJson(doc, out);

    return out;
}
#endif

void R33SlaveController::onClientDisconnected(int sockfd) {
    ESP_LOGW(TAG, "onClientDisconnected called, sockfd=%d", sockfd);
    if (wsEvents) {
        xEventGroupSetBits(wsEvents, WS_EVENT_DISCONNECTED);
    }
}