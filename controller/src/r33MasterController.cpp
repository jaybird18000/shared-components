#include "r33MasterController.h"
#include "WsServer.h"
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
#include "DebugControl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "String.h"
#include "cstring"
#include "cmath"

static const char* TAG = "r33MasterController";

static ValvesController valves;
static GenRelay genRelay;

void r33MasterController::start() {

    TaskUtilities::createTaskWithQueue(TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK, r33MasterControllerTask, sizeof(TaskUtilities::MsgItem));

    valves.init();
    genRelay.init();

}

void r33MasterController::r33MasterControllerTask(void* param) {
    while (true) {
        TaskUtilities::MsgItem item;
//        if (xQueueReceive(webClientQueue_, &item, portMAX_DELAY)) {
        if (TaskUtilities::waitOnQueue(TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK, &item, portMAX_DELAY) == pdTRUE)
        {

            std::string payload(item.data, item.len);  // local, safe
//            ESP_LOGI(TAG, "Processing web payload from fd=%d: %s",
//                     item.sockfd, payload.c_str());
            if(item.fromBrowser)
            {
                ClientsList::instance().updateLastSeen(item.sockfd);
                const char* role = item.fromBrowser ? "BROWSER" : "SLAVE";
                //ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());
                r33MasterController::handleWebClientMsg(item.sockfd, payload);

            }
            else
            {
                const char* role = item.fromBrowser ? "BROWSER" : "SLAVE";
                //ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());
                r33MasterController::handleSlaveClientMsg(item.sockfd, payload);
            }

        }
    }
}

void r33MasterController::handleWebClientMsg(int sockfd, const std::string &message)
{

        if (message.find("start_generator") != std::string::npos) {
            ESP_LOGW(TAG, "handleWebClientMsg:start_generator command received: Len %d", (int)message.size());

            debugServices::postDebug(std::string("Start generator cmd rcvd "));

            std::vector<ClientInfo*> slaves = ClientsList::instance().getSlaves();
            if (slaves.empty() || slaves[0] == nullptr) {
                ESP_LOGW(TAG, "No slave connected — cannot forward command");
                return;
            }
            // need to forward this message to client so we need to get client fd                
            int slaveSockfd = slaves[0]->sockfd;
//            ESP_LOGW(TAG, "forward command to slave %d ", slaveSockfd);

            TaskUtilities::MsgItem item{};
            item.sockfd = slaveSockfd;
            item.fromClient = false;
            item.fromMaster = true;
            item.fromBrowser = true;
            item.fromTask = TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK;
            item.toTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
            item.msgType = TaskUtilities::MsgTypes::DATA;
            item.frameType = HTTPD_WS_TYPE_TEXT;
            item.setMsg(message);
            item.len = message.size();
            auto bytes = item.serialize();

            WsServer::instance().sendBinaryMsg(slaveSockfd, bytes.data(), bytes.size());

            return;
        }
        if (message.find("stop_generator") != std::string::npos) {
//            ESP_LOGW(TAG, "handleWebClientMsg:stop_generator command received: Len %d", (int)message.size());
//            genRelay.setEnabled(!genRelay.enabled());
            //genRelay.momentary();
            debugServices::postDebug(std::string("Stop Generator cmd rcvd "));

            std::vector<ClientInfo*> slaves = ClientsList::instance().getSlaves();
            if (slaves.empty() || slaves[0] == nullptr) {
                ESP_LOGW(TAG, "No slave connected — cannot forward command");
                return;
            }
            // need to forward this message to client so we need to get client fd                
            int slaveSockfd = slaves[0]->sockfd;
//            ESP_LOGW(TAG, "forward command to slave %d ", slaveSockfd);
            TaskUtilities::MsgItem item{};
            item.sockfd = slaveSockfd;
            item.fromClient = false;
            item.fromMaster = true;
            item.fromBrowser = true;
            item.fromTask = TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK;
            item.toTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
            item.msgType = TaskUtilities::MsgTypes::DATA;
            item.frameType = HTTPD_WS_TYPE_TEXT;
            item.setMsg(message);
            item.len = message.size();
            auto bytes = item.serialize();

            WsServer::instance().sendBinaryMsg(slaveSockfd, bytes.data(), bytes.size());
       
            return;
        }
        if (message.find("toggle_gen_valve") != std::string::npos) {
            ESP_LOGW(TAG, "handleWebClientMsg:toggle_gen_valve command received");
            if (valves.generatorValve().state() == ValveState::Open) {
                valves.closeGenerator();
            } else {
                valves.openGenerator();
            }
            debugServices::postDebug("Generator valve command received");

            return;
        }
        if (message.find("toggle_ac_valve") != std::string::npos) {
            ESP_LOGW(TAG, "handleWebClientMsg:toggle_ac_valve command received");
            if (valves.acValve().state() == ValveState::Open) {
                valves.closeAirConditioner();
            } else {
                valves.openAirConditioner();
            }
            debugServices::postDebug("AC valve command received");

            return;
        }
        ESP_LOGI(TAG, "handleWebClientMsg: unhandled message from web client: %s", message.c_str());
}

void r33MasterController::handleSlaveClientMsg(int sockfd, const std::string& message)
{
    DynamicJsonDocument doc(256);
    deserializeJson(doc, message);

    const char* type = doc["type"];

    if(type != nullptr)
    {
        if (strcmp(type, "ac_update") == 0) {
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
            SharedDataStore::set(msg.voltage, msg.current, msg.frequency, msg.watts, msg.genStatus);

        }
        else if (strcmp(type, "command") == 0) {
            ESP_LOGI(TAG, "Rcvd Slave COMMAND json message from %d: %s", sockfd, message.c_str());
            ESP_LOGI(TAG, "Slave COMMAND NOT IMPLEMENTED");
    //
    //        String action = doc["payload"]["action"];
    //        ...
        }
        else if(strcmp(type,"clientRegistration") == 0)
        {
            if(doc["source"] == "slave")
            {
                ESP_LOGI(TAG, "Registering slave client: %d", sockfd);
                ClientsList::instance().updateClientType(sockfd, ClientInfo::ClientType::SLAVE);
            }
            else if(doc["source"] == "sunshade")
            {
                ESP_LOGI(TAG, "Registering sunshade client: %d", sockfd);
                ClientsList::instance().updateClientType(sockfd, ClientInfo::ClientType::SUNSHADE);
            }
            else
            {
                ESP_LOGW(TAG,"Master rcvd unkown clientRegistration of %s ", doc["source"]);
            }
        }
        else
        {
            //if (DebugControl::enabled(Item::MASTER_CONTROLLER))
            {
                ESP_LOGW(TAG, "Rcvd json message from %d ignoring: %s", sockfd, message.c_str());
            }
        }
    }
    else
    {
        //if (DebugControl::enabled(Item::MASTER_CONTROLLER))
        {
            ESP_LOGW(TAG, "JSON message has no 'type' field from %d: %s", sockfd, message.c_str());
        }
    }
}