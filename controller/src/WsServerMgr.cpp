#include "WsServerMgr.h"
#include "WsServer.h"
#include "clientsList.h"
#include "GenRelay.h"
#include "nvsMgr.h"
#include "SharedDataStore.h"
#include "ArduinoJson.h"
#include "AcUpdateTask.h"
#include "Valves.h"
#include "LedController.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "String.h"
#include "cstring"
#include "cmath"

static const char* TAG = "WsServerMgr";

static ValvesController valves;
static GenRelay genRelay;

// Analog gating state (2% change threshold)
static float s_lastVoltage = NAN;
static float s_lastCurrent = NAN;
static bool  s_haveLastAnalog = false;

// Digital gating state
static std::string s_lastGenState;
static std::string s_lastGenValve;
static std::string s_lastAcValve;
static bool s_haveLastDigital = false;

QueueHandle_t WsServerMgr::webClientQueue_ = nullptr;
QueueHandle_t WsServerMgr::slaveClientQueue_ = nullptr;
QueueHandle_t WsServerMgr::g_acUpdateQueue = nullptr;

static std::string extractJsonField(const std::string& payload, const char* field)
{
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = payload.find(needle);
    if (pos == std::string::npos) return {};
    size_t colon = payload.find(':', pos);
    if (colon == std::string::npos) return {};
    size_t start = payload.find('"', colon + 1);
    if (start == std::string::npos) return {};
    size_t end = payload.find('"', start + 1);
    if (end == std::string::npos) return {};
    return payload.substr(start + 1, end - start - 1);
}
static bool extractJsonBool(const std::string& payload, const char* field)
{
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = payload.find(needle);
    if (pos == std::string::npos) return false;
    size_t colon = payload.find(':', pos);
    if (colon == std::string::npos) return false;
    size_t start = payload.find_first_not_of(" \t", colon + 1);
    if (start == std::string::npos) return false;
    std::string value = payload.substr(start, 4);
    if (value == "true") return true;
    if (value == "false") return false;
    return false;
}
static int extractJsonInt(const std::string& payload, const char* field)
{
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = payload.find(needle);
    if (pos == std::string::npos) return -1;

    size_t colon = payload.find(':', pos);
    if (colon == std::string::npos) return -1;

    // Find start of number (skip spaces)
    size_t start = payload.find_first_of("0123456789", colon + 1);
    if (start == std::string::npos) return -1;

    // Find end of number
    size_t end = payload.find_first_not_of("0123456789", start);

    return std::stoi(payload.substr(start, end - start));
}
static std::string floatToString(float value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    return std::string(buf);
}

void WsServerMgr::start() {
    webClientQueue_ = xQueueCreate(20, sizeof(WorkItem));
    slaveClientQueue_ = xQueueCreate(20, sizeof(WorkItem));
    g_acUpdateQueue = xQueueCreate(20, sizeof(SharedAcData_t));
    xTaskCreate(webClientWorkerTask, "WebClientServerMgrWorker", 4096, nullptr, 4, nullptr);
    xTaskCreate(slaveClientWorkerTask, "SlaveClientServerMgrWorker", 4096, nullptr, 4, nullptr);
    xTaskCreate(pingTask, "WsServerMgrPing", 4096, nullptr, 4, nullptr);
    xTaskCreate(AcUpdateTask, "WsServerMgrAcUpdate", 4096, nullptr, 4, nullptr);
    xTaskCreate(broadcastStatusTask, "WsBroadcastStatus", 4096, nullptr, 4, nullptr);
//    acMonitor.init();
    valves.init();
    genRelay.init();
    SharedDataStore::init();
}

void WsServerMgr::enqueueWebClient(int sockfd, const std::string& payload) {
    WorkItem item{};
    item.sockfd = sockfd;

    if (payload.size() > sizeof(item.data)) {
        item.len = sizeof(item.data);
    } else {
        item.len = payload.size();
    }

    memcpy(item.data, payload.data(), item.len);
    xQueueSend(webClientQueue_, &item, 0);
}

void WsServerMgr::enqueueSlaveClient(int sockfd, const std::string& payload) {
    WorkItem item{};
    item.sockfd = sockfd;

    if (payload.size() > sizeof(item.data)) {
        item.len = sizeof(item.data);
    } else {
        item.len = payload.size();
    }

    memcpy(item.data, payload.data(), item.len);
    xQueueSend(slaveClientQueue_, &item, 0);
}

void WsServerMgr::webClientWorkerTask(void* param) {
    while (true) {
        WorkItem item;
        if (xQueueReceive(webClientQueue_, &item, portMAX_DELAY)) {
            std::string payload(item.data, item.len);  // local, safe
//            ESP_LOGI(TAG, "Processing web payload from fd=%d: %s",
//                     item.sockfd, payload.c_str());

            WsServerMgr::handleWebClientMsg(item.sockfd, payload);
            // TODO: your heavy logic here
        }
    }
}
void WsServerMgr::slaveClientWorkerTask(void* param) {
    while (true) {
        WorkItem item;
        if (xQueueReceive(slaveClientQueue_, &item, portMAX_DELAY)) {

            std::string payload(item.data, item.len);  // local, safe                     
//            ESP_LOGI(TAG, "Processing slave payload from fd=%d: %s",
//                     item.sockfd, payload.c_str());
            WsServerMgr::handleSlaveClientMsg(item.sockfd, payload);
            // TODO: your heavy logic here
        }
    }
}
void WsServerMgr::pingTask(void* param) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        auto& list = ClientsList::instance();

        list.forEachClient([&](int fd, ClientInfo& info) {
            if(info.type != ClientInfo::ClientType::MASTER)
            {
                auto now = std::chrono::steady_clock::now();

                auto diffMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - info.lastSeen
                ).count();

                if (!info.pingSent) {
                    WsServer::instance().sendPingMsg(fd);
                    list.updatePingSent(fd, true);
//                    ESP_LOGI(TAG,"sent ping to fd %d - diffMs %lld", fd, diffMs);
                }
                else if (diffMs > 3000) {
                    ESP_LOGW(TAG, "Client fd=%d timed out, diff %lldms, closing ", fd, diffMs);
                    httpd_sess_trigger_close(WsServer::instance().serverHandle(), fd);
                    list.removeClient(fd);
                }
            }
        });
    }
}
 void WsServerMgr::AcUpdateTask(void* param)
{
    SharedAcData_t msg;

    while (true) {
        if (xQueueReceive(g_acUpdateQueue, &msg, portMAX_DELAY)) {
            // ESP_LOGI(TAG,"Rcvd AC Update from slave, store in sharedDataStore");
            // ESP_LOGI(TAG,"Voltage: %0.1f Currrent: %0.1f freq: %0.1f genStat: %s", msg.voltage, msg.current, msg.frequency, msg.genStatus);
            
            SharedDataStore::set(msg.voltage, msg.current, msg.frequency, msg.watts, msg.genStatus);
        }
    }
}
void WsServerMgr::broadcastStatusTask(void *param)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        auto& list = ClientsList::instance();
//        ESP_LOGI(TAG,"broadcast loop client size %d", list.size());
// static int bcastMsgCounter = 0;
// static int loopCounter = 0;
//         // Only postDebug every 5 loops
//         if (((loopCounter++ % 5) == 0) && (loopCounter > 20))
//         {
// WsServer::instance().postDebug("broadcastStatus loop msg %d", bcastMsgCounter++);
//         }
        if (list.size() > 0)
        {
            broadcastStatus();
        }
//        LedController::testLeds();
        LedController::instance().blink(0, 500);

    }
}
void WsServerMgr::handleWebClientMsg(int sockfd, const std::string &message)
{

        if (message.find("start_generator") != std::string::npos) {
            ESP_LOGW(TAG, "handleWebClientMsg:start_generator command received: Len %d", (int)message.size());

            WsServer::instance().postDebug(std::string("Start generator cmd rcvd "));

            DynamicJsonDocument doc(256);

            doc["type"] = "command";
            doc["source"] = "master";

            JsonObject p = doc["payload"].to<JsonObject>();
            p["action"] = "start_generator";

            std::string out;
            serializeJson(doc, out);

//            ClientsList::instance().debugPrintAll();
            if(WifiMgr::instance().isMaster())
            {
                std::vector<ClientInfo*> slaves = ClientsList::instance().getSlaves();
                if (slaves.empty() || slaves[0] == nullptr) {
                    ESP_LOGW(TAG, "No slave connected — cannot forward command");
                    return;
                }
                // need to forward this message to client so we need to get client fd                
                int slaveSockfd = slaves[0]->sockfd;
    //            ESP_LOGW(TAG, "forward command to slave %d ", slaveSockfd);

                WsServer::instance().sendTextMsg(slaveSockfd, out);
            }
            else  // must be the slave
            {
                genRelay.momentaryStart();
            }
            return;
        }
        if (message.find("stop_generator") != std::string::npos) {
//            ESP_LOGW(TAG, "handleWebClientMsg:stop_generator command received: Len %d", (int)message.size());
//            genRelay.setEnabled(!genRelay.enabled());
            //genRelay.momentary();
            WsServer::instance().postDebug(std::string("Stop Generator cmd rcvd "));

            DynamicJsonDocument doc(256);

            doc["type"] = "command";
            doc["source"] = "master";

            JsonObject p = doc["payload"].to<JsonObject>();
            p["action"] = "stop_generator";

            std::string out;
            serializeJson(doc, out);
            // need to forward this message to client so we need to get client fd
//            ClientsList::instance().debugPrintAll();
            if(WifiMgr::instance().isMaster())
            {
                std::vector<ClientInfo*> slaves = ClientsList::instance().getSlaves();
                if (slaves.empty() || slaves[0] == nullptr) {
                    ESP_LOGW(TAG, "No slave connected — cannot forward command");
                    return;
                }
                // need to forward this message to client so we need to get client fd                
                int slaveSockfd = slaves[0]->sockfd;
    //            ESP_LOGW(TAG, "forward command to slave %d ", slaveSockfd);

                WsServer::instance().sendTextMsg(slaveSockfd, out);
            }
            else  // must be the slave
            {
                genRelay.momentaryStop();
            }           
            return;
        }
        if (message.find("toggle_gen_valve") != std::string::npos) {
            ESP_LOGW(TAG, "handleWebClientMsg:toggle_gen_valve command received");
            if (valves.generatorValve().state() == ValveState::Open) {
                valves.closeGenerator();
            } else {
                valves.openGenerator();
            }
            WsServer::instance().postDebug("Generator valve command received");

            broadcastStatus(); // force immediate status
            return;
        }
        if (message.find("toggle_ac_valve") != std::string::npos) {
            ESP_LOGW(TAG, "handleWebClientMsg:toggle_ac_valve command received");
            if (valves.acValve().state() == ValveState::Open) {
                valves.closeAirConditioner();
            } else {
                valves.openAirConditioner();
            }
            WsServer::instance().postDebug("AC valve command received");

            broadcastStatus(); // force immediate status
            return;
        }
        if (message.find("save_STA_wifi") != std::string::npos) {
            std::string ssid = extractJsonField(message, "ssid");
            std::string password = extractJsonField(message, "password");
            bool isMaster = extractJsonBool(message, "isMaster");
            if (!ssid.empty()) {
                NvsMgr::instance().saveSTA_Config(ssid, password, isMaster);
                WsServer::instance().postDebug("STAWiFi configuration saved");

                WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"sta_config_saved\"}");
                return;
            }
            else
            {
                WsServer::instance().postDebug("STAWiFi configuration missing ssid");

                WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"sta_config_error\",\"message\":\"SSID cannot be empty\"}");
                return;
            }
        }
        if (message.find("save_AP_wifi") != std::string::npos) {
            std::string ssid = extractJsonField(message, "ssid");
            std::string password = extractJsonField(message, "password");
            std::string ipAddress = extractJsonField(message, "ipAddress");
            std::string gateway = extractJsonField(message, "gateway");
            std::string netmask = extractJsonField(message, "netmask");
            if (!ssid.empty()) {
                NvsMgr::instance().saveAP_Config(ssid, password, ipAddress, gateway, netmask);
                WsServer::instance().postDebug("AP WiFi configuration saved");

                WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"ap_config_saved\"}");
                return;
            }
            else
            {
                WsServer::instance().postDebug("AP WiFi configuration missing ssid");

                WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"ap_config_error\",\"message\":\"SSID cannot be empty\"}");
                return;
            }
        }
        if (message.find("subscribe_status") != std::string::npos) {
            ESP_LOGI("handleWebClientMsg", "rcvd subscribe status msg");


            WifiConfig config = NvsMgr::instance().currentSTA_Config();
            if (!config.ssid.empty()) {

                WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"STA_wifi_config\",\"ssid\":\"" + config.ssid + "\",\"password\":\"" + config.password + "\",\"isMaster\":" + (config.isMaster ? "true" : "false") + "}");
                ESP_LOGI("handleWebClientMsg", "sent staConfig to client") ;
            }
            else
            {

                WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"STA_wifi_config_error\",\"ssid\":\"\",\"password\":\"\"}");
                ESP_LOGI("handleWebClientMsg", "no staConfig to send to client") ;
            }

            WifiConfig ap_config = NvsMgr::instance().currentAP_Config();
            if (!ap_config.ssid.empty()) {

                WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"AP_wifi_config\",\"ssid\":\"" + ap_config.ssid + "\",\"password\":\"" + ap_config.password + "\",\"ipAddress\":\"" + ap_config.ipAddress + "\",\"gateway\":\"" + ap_config.gateway + "\",\"netmask\":\"" + ap_config.netmask + "\"}");
                ESP_LOGI("handleWebClientMsg", "sent apConfig to client") ;
            }
            else
            {

                WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"AP_wifi_config_error\",\"ssid\":\"\",\"password\":\"\",\"ipAddress\":\"\",\"gateway\":\"\",\"netmask\":\"\"}");
                ESP_LOGI("handleWebClientMsg", "no apConfig to send to client") ;
            }
            return;
        }
        if (message.find("subscribe_debug") != std::string::npos) {

            int webClientLastReceivedDebugMsgCtr = extractJsonInt(message, "lastDebugId");
            ESP_LOGI("handleWebClientMsg", "rcvd subscribe debug msg counter = %d", webClientLastReceivedDebugMsgCtr);
            WsServer::instance().sendDebugMsgsSince(webClientLastReceivedDebugMsgCtr, sockfd);
            return;
        }
}

void WsServerMgr::handleSlaveClientMsg(int sockfd, const std::string& message)
{
    DynamicJsonDocument doc(256);
    deserializeJson(doc, message);

    std::string type = doc["type"];

    if (type == "ac_update") {
        float voltage = doc["payload"]["voltage"];
        float current = doc["payload"]["current"];
        float freq = doc["payload"]["frequency"];
        float watts = doc["payload"]["watts"];
        std::string genStateStr = doc["payload"]["genStatus"];
        SharedAcData_t msg = {};
        msg.voltage     = voltage;
        msg.current     = current;
        msg.frequency   = freq;
        msg.watts       = watts;
        msg.timestampMs = esp_timer_get_time() / 1000ULL;

        // ⭐ YES — copy the std::string directly into the char buffer
        std::strncpy(msg.genStatus, genStateStr.c_str(), sizeof(msg.genStatus) - 1);
        msg.genStatus[sizeof(msg.genStatus) - 1] = '\0';

        // Push into queue (non-blocking)
        xQueueSend(g_acUpdateQueue, &msg, 0);
    }
    else if (type == "command") {
        ESP_LOGI(TAG, "Rcvd Slave COMMAND json message from %d: %s", sockfd, message.c_str());
        ESP_LOGI(TAG, "Slave COMMAND NOT IMPLEMENTED");
//
//        String action = doc["payload"]["action"];
//        ...
    }
    else if(type == "debug") {
        int id = doc["id"];
        std::string msg = doc["message"];
//        ESP_LOGI(TAG, "Rcvd Slave DEBUG json message from %d: id=%d msg=%s", sockfd, id, msg.c_str());
        WsServer::instance().postDebug(msg.c_str());
    }
}

void WsServerMgr::broadcastStatus()
{
    // master has the valves 
    if (WifiMgr::instance().isMaster())
    {
        valves.update();
    }

    // Read current values
    // float voltage = acMonitor.readVoltage();
    // float current = acMonitor.readCurrent();
    // std::string genState = genRelay.stateText();
    // slave sent the ac info via a message and the SharedDataStore is updated with the latest values
    SharedAcData_t ac = SharedDataStore::get();
    float voltage = ac.voltage;
    float current = ac.current;
    float frequency = ac.frequency;
    std::string genState = ac.genStatus;

    std::string genValve = valves.generatorValve().statusText();
    std::string acValve  = valves.acValve().statusText();

    bool send = false;

    // --- DIGITAL CHANGE DETECTION ---
    if (!s_haveLastDigital ||
        genState != s_lastGenState ||
        genValve != s_lastGenValve ||
        acValve  != s_lastAcValve)
    {
        send = true;
    }

    // --- ANALOG CHANGE DETECTION (2%) ---
    auto changedBy2Percent = [](float oldVal, float newVal) -> bool {
        float base = fabsf(oldVal);
        if (base < 0.01f) base = 0.01f;
        float delta = fabsf(newVal - oldVal) / base;
        return (delta >= 0.02f);
    };

    bool analogChanged = false;

    if (!s_haveLastAnalog) {
        analogChanged = true;
    } else {
        bool vChanged = changedBy2Percent(s_lastVoltage, voltage);
        bool cChanged = changedBy2Percent(s_lastCurrent, current);
        analogChanged = (vChanged || cChanged);
        // ESP_LOGI(TAG, "calc change: v %.2f->%.2f (%s) c %.2f->%.2f (%s)",
        //          s_lastVoltage, voltage, vChanged ? "CHANGED" : "same",
        //          s_lastCurrent, current, cChanged ? "CHANGED" : "same");

    }

    if (analogChanged)
        send = true;


    if (!send) {
//        return;
    }

    // Update last-known values
    s_lastVoltage = voltage;
    s_lastCurrent = current;
    s_haveLastAnalog = true;

    s_lastGenState = genState;
    s_lastGenValve = genValve;
    s_lastAcValve  = acValve;
    s_haveLastDigital = true;

    // Build JSON
bool slaveConnected = !ClientsList::instance().getSlaves().empty();
    const char* slaveConnection = slaveConnected ? "connected" : "disconnected";

    bool masterConnected = false;
    ClientsList::instance().forEachClient([&](int fd, ClientInfo& info) {
      if (info.type == ClientInfo::ClientType::MASTER) {
        masterConnected = true;
      }
    });
    const char* masterConnection = masterConnected ? "connected" : "disconnected";
    std::string currentSSID = WifiMgr::instance().currentWifiSSID();
    std::string wifiStatus = WifiMgr::instance().statusText();

    char buffer[512];
    snprintf(buffer, sizeof(buffer),
        "{\"type\":\"status\",\"ssid\":\"%s\",\"wifi\":\"%s\",\"generator\":\"%s\","
        "\"generatorValve\":\"%s\",\"acValve\":\"%s\",\"acVoltage\":\"%s\",\"acCurrent\":\"%s\",\"acFrequency\":\"%s\",\"slaveConnection\":\"%s\",\"masterConnection\":\"%s\"}",
        currentSSID.c_str(),
        wifiStatus.c_str(),
        genState.c_str(),
        genValve.c_str(),
        acValve.c_str(),
        floatToString(voltage).c_str(),
        floatToString(current).c_str(),
        floatToString(frequency).c_str(),
        slaveConnection,
        masterConnection

    );

    WsServer::instance().broadcastText(std::string(buffer), WsServer::AudienceType::BROWSERS);
}

