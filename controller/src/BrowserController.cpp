#include "BrowserController.h"
#include "TaskUtilities.h"
#include "Utilities.h"
#include "nvsMgr.h"
#include "ClientsList.h"
#include "DebugServices.h"
#include "DebugControl.h"
#include "LedController.h"
#include "WsServer.h"
#include "SharedDataStore.h"
#include "GenRelay.h"
#include "Valves.h"
#include "version.h"
#include "OtaManager.h"
#include "MdnsMgr.h"
#include "DeviceConfig.h"
#include "statusDb.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>


static constexpr const char* TAG = "BrowserController";
static auto& valves = ValvesController::instance();
static GenRelay genRelay;

BrowserController& BrowserController::instance() {
    static BrowserController inst;
    return inst;
}

static float s_lastVoltage = NAN;
static float s_lastCurrent = NAN;
static bool  s_haveLastAnalog = false;

// Digital gating state
static std::string s_lastGenState;
static std::string s_lastGenValve;
static std::string s_lastAcValve;
static bool s_haveLastDigital = false;

void BrowserController::begin()
{
        TaskUtilities::createTaskWithQueue(TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK, messageTaskEntry, sizeof(TaskUtilities::MsgItem));

        xTaskCreate(broadcastStatusTask, "browserBroadcastStatus", 4096, nullptr, 4, nullptr);
}

void BrowserController::messageTaskEntry(void *param)
{
    BrowserController::messageTaskLoop();
}

void BrowserController::messageTaskLoop()
{
        ESP_LOGI(TAG, "Starting Browser Controller message task loop");
        while (true) 
        {
        TaskUtilities::MsgItem item;
//        if (xQueueReceive(webClientQueue_, &item, portMAX_DELAY)) {
        if (TaskUtilities::waitOnQueue(TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK, &item, portMAX_DELAY) == pdTRUE)
        {
            std::string payload(item.data, item.len);  // local, safe
//            ESP_LOGI(TAG, "Processing web payload from fd=%d: %s",
//                     item.sockfd, payload.c_str());
            if(item.fromBrowser)
            {
                ClientsList::instance().updateLastSeen(item.sockfd);
                const char* role = item.fromBrowser ? "BROWSER" : "SLAVE";
                //ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());
                BrowserController::handleWebClientMsg(item.sockfd, item);

            }
            else
            {
                const char* role = item.fromBrowser ? "BROWSER" : "SLAVE";
                //ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());
                BrowserController::handleSlaveClientMsg(item.sockfd, item);
            }

        }
    }
}

void BrowserController::broadcastStatusTask(void *param)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        auto& list = ClientsList::instance();
//        ESP_LOGI(TAG,"broadcast loop client size %d", list.size());

        if (list.size() > 0)
        {
            broadcastStatus();
        }
//        LedController::testLeds();
        LedController::instance().blink(0, 500);

    }
}

void BrowserController::handleWebClientMsg(int sockfd, TaskUtilities::MsgItem& item)
{
    std::string message(item.data, item.len);  // local, safe

    if ((message.find("start_generator") != std::string::npos) ||
        (message.find("stop_generator") != std::string::npos))
        {
        //ESP_LOGW(TAG, "handleWebClientMsg:start_generator command received: Len %d", (int)message.size());

        if(DeviceConfig::instance().isMasterDevice()) {
            item.toTask = TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK;
            TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK, &item, 0);
        }
        else if(DeviceConfig::instance().isSlaveDevice()) {
            item.toTask = TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK;
            TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK, &item, 0);
        }
        else
        {
            debugServices::postDebug("Received start/stop generator command, but device is neither master nor slave");
        }
        return;
    }

    if ((message.find("open_gen_valve") != std::string::npos) ||
        (message.find("close_gen_valve") != std::string::npos) ||
        (message.find("open_ac_valve") != std::string::npos) ||
        (message.find("close_ac_valve") != std::string::npos))
    {
        //ESP_LOGW(TAG, "handleWebClientMsg:valve command received: Len %d", (int)message.size());

        item.toTask = TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK;

        TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK, &item, 0);
        return;
    }    
   
    if (message.find("save_STA_wifi") != std::string::npos) {
        std::string ssid = Utilities::extractJsonField(message, "ssid");
        std::string password = Utilities::extractJsonField(message, "password");
        bool isMaster = Utilities::extractJsonBool(message, "isMaster");
        if (!ssid.empty()) {
            NvsMgr::instance().saveSTA_Config(ssid, password, isMaster);
            debugServices::postDebug("STAWiFi configuration saved");

            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"sta_config_saved\"}");
            return;
        }
        else
        {
            debugServices::postDebug("STAWiFi configuration missing ssid");

            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"sta_config_error\",\"message\":\"SSID cannot be empty\"}");
            return;
        }
    }
    if (message.find("save_AP_wifi") != std::string::npos) {
        std::string ssid = Utilities::extractJsonField(message, "ssid");
        std::string password = Utilities::extractJsonField(message, "password");
        std::string ipAddress = Utilities::extractJsonField(message, "ipAddress");
        std::string gateway = Utilities::extractJsonField(message, "gateway");
        std::string netmask = Utilities::extractJsonField(message, "netmask");
        if (!ssid.empty()) {
            NvsMgr::instance().saveAP_Config(ssid, password, ipAddress, gateway, netmask);
            debugServices::postDebug("AP WiFi configuration saved");

            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"ap_config_saved\"}");
            return;
        }
        else
        {
            debugServices::postDebug("AP WiFi configuration missing ssid");

            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"ap_config_error\",\"message\":\"SSID cannot be empty\"}");
            return;
        }
    }
    if (message.find("subscribe_status") != std::string::npos) {
        ESP_LOGI(TAG, "rcvd subscribe status msg");


        WifiConfig config = NvsMgr::instance().currentSTA_Config();
        if (!config.ssid.empty()) {

            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"STA_wifi_config\",\"ssid\":\"" + config.ssid + "\",\"password\":\"" + config.password + "\",\"isMaster\":" + (config.isMaster ? "true" : "false") + "}");
            ESP_LOGI(TAG, "sent staConfig to client") ;
        }
        else
        {

            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"STA_wifi_config_error\",\"ssid\":\"\",\"password\":\"\"}");
            ESP_LOGI(TAG, "no staConfig to send to client") ;
        }

        WifiConfig ap_config = NvsMgr::instance().currentAP_Config();
        if (!ap_config.ssid.empty()) {

            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"AP_wifi_config\",\"ssid\":\"" + ap_config.ssid + "\",\"password\":\"" + ap_config.password + "\",\"ipAddress\":\"" + ap_config.ipAddress + "\",\"gateway\":\"" + ap_config.gateway + "\",\"netmask\":\"" + ap_config.netmask + "\"}");
            ESP_LOGI(TAG, "sent apConfig to client") ;
        }
        else
        {

            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"AP_wifi_config_error\",\"ssid\":\"\",\"password\":\"\",\"ipAddress\":\"\",\"gateway\":\"\",\"netmask\":\"\"}");
            ESP_LOGI(TAG, "no apConfig to send to client") ;
        }
        return;
    }
    if (message.find("subscribe_debug") != std::string::npos) {

        int webClientLastReceivedDebugMsgCtr = Utilities::extractJsonInt(message, "lastDebugId");
        ESP_LOGI("handleWebClientMsg", "rcvd subscribe debug msg counter = %d", webClientLastReceivedDebugMsgCtr);
        //WsServer::instance().sendDebugMsgsSince(webClientLastReceivedDebugMsgCtr, sockfd);
        debugServices::sendDebugMsgsSince(webClientLastReceivedDebugMsgCtr, sockfd);
        return;
    }
    if (message.find("save_ota_url") != std::string::npos) 
    {
        std::string updateServerHost = Utilities::extractJsonField(message, "host");
        std::string updateServerPort = Utilities::extractJsonField(message, "port");
        ESP_LOGI(TAG, "Received save_ota_url with host=%s, port=%s", updateServerHost.c_str(), updateServerPort.c_str());
        if (!updateServerHost.empty() && !updateServerPort.empty()) 
        {
            NvsMgr::instance().saveUpdateServer(updateServerHost, updateServerPort);
            debugServices::postDebug("Update server URL saved");
            WsServer::instance().sendTextMsg(sockfd, "{\"type\":\"ota_url_saved\"}");
        }
        return;
    }
    if ((message.find("get_ota_url") != std::string::npos) ||
        (message.find("get_ota_server_status") != std::string::npos) ||
        (message.find("get_current_version") != std::string::npos) ||
        (message.find("check_updates") != std::string::npos) ||
        (message.find("start_update") != std::string::npos)) 
    {
        TaskUtilities::sendToQueue(TaskUtilities::TaskNames::OTA_MANAGER_MESSAGE_TASK, &item, 0);
        return;
    }
    #if 0
    if (message.find("get_ota_url") != std::string::npos) {
        std::string savedUrl = NvsMgr::instance().getUpdateServerUrl();
        std::string json = "{\"type\":\"ota_url_loaded\",\"url\":\"" + savedUrl + "\"}";
        WsServer::instance().sendTextMsg(sockfd, json);
        return;
    }
    if (message.find("get_current_version") != std::string::npos) {
        std::string currentVersion = getFirmwareVersion();
        std::string json = "{\"type\":\"current_version_info\",\"version\":\"" + currentVersion + "\"}";
        WsServer::instance().sendTextMsg(sockfd, json);
        return;
    }
    if (message.find("check_updates") != std::string::npos) {
        std::string updateServerUrl = Utilities::extractJsonField(message, "update_server_url");
        if (!updateServerUrl.empty()) {
            debugServices::postDebug("Checking for firmware updates...");
            
            // Create OTA task with its own stack
            OtaTaskParams* params = new OtaTaskParams();
            params->updateServerUrl = updateServerUrl;
            params->operation = 0;  // check_updates
            
            xTaskCreate(otaTaskFunction, "OtaTask", 4096, params, 5, &otaTaskHandle);
        }
        return;
    }
    if (message.find("start_update") != std::string::npos) {
        std::string updateServerUrl = Utilities::extractJsonField(message, "update_server_url");
        if (!updateServerUrl.empty()) {
            debugServices::postDebug("Starting firmware update...");
            
            // Create OTA task with its own stack
            OtaTaskParams* params = new OtaTaskParams();
            params->updateServerUrl = updateServerUrl;
            params->operation = 1;  // start_update
            
            xTaskCreate(otaTaskFunction, "OtaUpdateTask", 4096, params, 5, &otaTaskHandle);
        }
        return;
    }
#endif
    ESP_LOGW(TAG, "Received message from web browser, ignoring: %s", message.c_str());
}

void BrowserController::handleSlaveClientMsg(int sockfd, TaskUtilities::MsgItem& item)
{
    std::string message(item.data, item.len);  // local, safe
    DynamicJsonDocument doc(256);

    deserializeJson(doc, message);

    const char* type = doc["type"];
    if(type != nullptr)
    {
        if (strcmp(type, "debug") == 0)
        {
            int id = doc["id"];
            std::string msg = doc["message"];
    //        ESP_LOGI(TAG, "Rcvd Slave DEBUG json message from %d: id=%d msg=%s", sockfd, id, msg.c_str());
            debugServices::postDebug(msg.c_str());
        }
        else
        {
            if (DebugControl::enabled(DebugItem::BROWSER_CONTROLLER)) {
                ESP_LOGW(TAG, "Received message from slave, ignoring: %s", message.c_str());
            }
        }
    }
    
}

void BrowserController::broadcastStatus()
{
    // master has the valves 
    if (DeviceConfig::instance().isMasterDevice())
    {
        valves.update();
    }

    // Read current values
    // float voltage = acMonitor.readVoltage();
    // float current = acMonitor.readCurrent();
    // std::string genState = genRelay.stateText();
    // slave sent the ac info via a message and the SharedDataStore is updated with the latest values
    SharedAcData_t ac = SharedDataStore::get();
//    float voltage = ac.voltage;
//    float current = ac.current;
//    float frequency = ac.frequency;
//    std::string genState = ac.genStatus;
//    std::string genValve = valves.generatorValve().statusText();
//    std::string acValve  = valves.acValve().statusText();

    float voltage = StatusDb::instance().getGeneratorVoltage();
    float current = StatusDb::instance().getGeneratorCurrent();
    float frequency = StatusDb::instance().getGeneratorFrequency();
    std::string genState = GenRelay::genStateToString(StatusDb::instance().getGeneratorState());
    std::string genValve = Valve::statusText(StatusDb::instance().getGeneratorValveState());
    std::string acValve  = Valve::statusText(StatusDb::instance().getACValveState());

    std::string testModeStatus = "";
    if (StatusDb::instance().getMasterTestMode())
    {
        testModeStatus += "Master";
    }
    if (StatusDb::instance().getSlaveTestMode())
    {
        testModeStatus += " Slave";
    }
    if (StatusDb::instance().getSunShadeTestMode())
    {
        testModeStatus += " SunShade";
    }
    if(testModeStatus.empty())
    {
        testModeStatus = "None";
    }

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
    std::string serverConnection;
    if (masterConnected) {
        serverConnection = "Master";
    }
    if (OtaManager::instance().isFirmwareServerOnline()) {
        if (!serverConnection.empty()) {
            serverConnection += "\\n";
        }
        serverConnection += "OTA Server";
    }
    if (serverConnection.empty()) {
        serverConnection = "none";
    }
    size_t browserCount = ClientsList::instance().getBrowsers().size();
    bool sunshadeConnected = !ClientsList::instance().getSunshades().empty();
    std::string clientConnections;
    if (slaveConnected) {
        clientConnections = "Slave";
    }
    if (sunshadeConnected) {
        if (!clientConnections.empty()) {
            clientConnections += ", ";
        }
        clientConnections += "SunShade";
    }
    if (clientConnections.empty()) {
        clientConnections = "none";
    }
    std::string currentSSID = WifiMgr::instance().currentWifiSSID();
    std::string currentIpAddress = WifiMgr::instance().currentIpAddress();
    int currentRssi = WifiMgr::instance().currentWifiRssi();
    std::string wifiStatus = WifiMgr::instance().statusText();
    bool apMode = WifiMgr::instance().isApMode();
        std::vector<MdnsHostInfo> mdnsHostInfos = MdnsMgr::instance().getHostInfos();
        std::sort(mdnsHostInfos.begin(), mdnsHostInfos.end(), [](const MdnsHostInfo& left, const MdnsHostInfo& right) {
            return std::lexicographical_compare(
                left.name.begin(), left.name.end(), right.name.begin(), right.name.end(),
                [](unsigned char leftCharacter, unsigned char rightCharacter) {
                    return std::tolower(leftCharacter) < std::tolower(rightCharacter);
                });
        });

        std::string mdnsResponders;
        for (const MdnsHostInfo& hostInfo : mdnsHostInfos) {
            if (!mdnsResponders.empty()) {
                mdnsResponders += "\\n";
            }
            mdnsResponders += hostInfo.name + " (" + hostInfo.ipAddress + ")";
        }
        if (mdnsResponders.empty()) {
            mdnsResponders = "none";
        }

        char buffer[1024];
        snprintf(buffer, sizeof(buffer),
                "{\"type\":\"status\",\"ssid\":\"%s\",\"ipAddress\":\"%s\",\"rssi\":%d,\"apMode\":%s,"
                "\"serverConnection\":\"%s\",\"browserCount\":%u,\"clientConnections\":\"%s\",\"mdnsResponders\":\"%s\","
                "\"wifi\":\"%s\",\"generator\":\"%s\","
                "\"generatorValve\":\"%s\",\"acValve\":\"%s\",\"acVoltage\":\"%s\",\"acCurrent\":\"%s\",\"acFrequency\":\"%s\","
                "\"slaveConnection\":\"%s\",\"masterConnection\":\"%s\",\"testMode\":\"%s\"}",
        currentSSID.c_str(),
        currentIpAddress.c_str(),
        currentRssi,
        apMode ? "true" : "false",
        serverConnection.c_str(),
        static_cast<unsigned>(browserCount),
        clientConnections.c_str(),
                mdnsResponders.c_str(),
        wifiStatus.c_str(),
        genState.c_str(),
        genValve.c_str(),
        acValve.c_str(),
        Utilities::floatToString(voltage).c_str(),
        Utilities::floatToString(current).c_str(),
        Utilities::floatToString(frequency).c_str(),
        slaveConnection,
        masterConnection,
        testModeStatus.c_str()

    );

    WsServer::instance().broadcastText(std::string(buffer), WsServer::AudienceType::BROWSERS);
}