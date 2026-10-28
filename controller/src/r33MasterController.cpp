#include "r33MasterController.h"
#include "WsServer.h"
#include "clientsList.h"
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
#include "statusDb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "String.h"
#include "cstring"
#include "cmath"

static const char* TAG = "r33MasterController";

static auto& valves = ValvesController::instance();

void r33MasterController::start() {

    TaskUtilities::createTaskWithQueue(TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK, r33MasterControllerTask, sizeof(TaskUtilities::MsgItem));

    valves.init();

}

void r33MasterController::r33MasterControllerTask(void* param) {
    while (true) {
        TaskUtilities::MsgItem item;
        if (TaskUtilities::waitOnQueue(TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK, &item, portMAX_DELAY) == pdTRUE)
        {
            // highjack the debug message and send to the browser
            if((item.fromClient) && (item.msgType == TaskUtilities::MsgTypes::DEBUG))
            {
                // send all debug messages from clients to the browser controller
                item.toTask = TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK;
                TaskUtilities::sendToQueue(TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK, &item, 0);
            }            
            else 
            {
                std::string payload(item.data, item.len);  // local, safe
    //            ESP_LOGI(TAG, "Processing web payload from fd=%d: %s",
    //                     item.sockfd, payload.c_str());
                ClientsList::instance().updateLastSeen(item.sockfd);
                const char* role = item.fromBrowser ? "BROWSER" : "SLAVE";
                if(item.fromBrowser){

                    //ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());
                    r33MasterController::handleBrowserClientMsg(item.sockfd, payload);

                }
                else
                {

                    //ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());
                    r33MasterController::handleWebSrvClientMsg(item.sockfd, payload);
                }
            }
        }
    }
}

void r33MasterController::handleBrowserClientMsg(int sockfd, const std::string &message)
{

        if (message.find("start_generator") != std::string::npos) {
            ESP_LOGW(TAG, "handleBrowserClientMsg:start_generator command received: Len %d", (int)message.size());

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
            item.toTask = TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK;
            item.msgType = TaskUtilities::MsgTypes::DATA;
            item.frameType = HTTPD_WS_TYPE_TEXT;
            item.setMsg(message);
            item.len = message.size();
            auto bytes = item.serialize();

            WsServer::instance().sendBinaryMsg(slaveSockfd, bytes.data(), bytes.size());

            return;
        }
        if (message.find("stop_generator") != std::string::npos) {
//            ESP_LOGW(TAG, "handleBrowserClientMsg:stop_generator command received: Len %d", (int)message.size());

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
            item.toTask = TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK;
            item.msgType = TaskUtilities::MsgTypes::DATA;
            item.frameType = HTTPD_WS_TYPE_TEXT;
            item.setMsg(message);
            item.len = message.size();
            auto bytes = item.serialize();

            WsServer::instance().sendBinaryMsg(slaveSockfd, bytes.data(), bytes.size());
       
            return;
        }
        if (message.find("open_gen_valve") != std::string::npos) {
            valves.openGenerator();
            debugServices::postDebug("Open generator valve command received");

            return;
        }
        if (message.find("close_gen_valve") != std::string::npos) {
            valves.closeGenerator();
            debugServices::postDebug("Close generator valve command received");

            return;
        }
        if (message.find("open_ac_valve") != std::string::npos) {
            valves.openAirConditioner();
            debugServices::postDebug("Open AC valve command received");

            return;
        }
        if (message.find("close_ac_valve") != std::string::npos) {
            valves.closeAirConditioner();
            debugServices::postDebug("Close AC valve command received");

            return;
        }
        ESP_LOGI(TAG, "handleBrowserClientMsg: unhandled message from web client: %s", message.c_str());
}

void r33MasterController::handleWebSrvClientMsg(int sockfd, const std::string& message)
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

            GenRelay::GenStates genState = static_cast<GenRelay::GenStates>(doc["payload"]["genStatus"].as<int>());

//            std::string genStateStr = doc["payload"]["genStatus"];

            SharedAcData_t msg = {};
            msg.voltage     = voltage;
            msg.current     = current;
            msg.frequency   = freq;
            msg.watts       = watts;
            msg.timestampMs = esp_timer_get_time() / 1000ULL;

            strcpy(msg.genStatus, GenRelay::genStateToString(genState));

            // ⭐ YES — copy the std::string directly into the char buffer
//            std::strncpy(msg.genStatus, genStateStr.c_str(), sizeof(msg.genStatus) - 1);
//            msg.genStatus[sizeof(msg.genStatus) - 1] = '\0';
            SharedDataStore::set(msg.voltage, msg.current, msg.frequency, msg.watts, msg.genStatus);
            StatusDb::instance().setAcData(voltage, current, freq, watts, genState);

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
            bool isSlave = false;
            bool isSunshade = false;    
            if(doc["source"] == "slave")
            {
                ESP_LOGI(TAG, "Registering SLAVE client: %d", sockfd);
                debugServices::postDebug("Registering SLAVE client: %d", sockfd);
                ClientsList::instance().updateClientType(sockfd, ClientInfo::ClientType::SLAVE);
                isSlave = true;
            }
            else if(doc["source"] == "sunshade")
            {
                ESP_LOGI(TAG, "Registering SUNSHADE client: %d", sockfd);
                debugServices::postDebug("Registering SUNSHADE client: %d", sockfd);
                ClientsList::instance().updateClientType(sockfd, ClientInfo::ClientType::SUNSHADE);
                isSunshade = true;
            }
            else
            {
                ESP_LOGW(TAG,"Master rcvd unkown clientRegistration of %s ", doc["source"]);
            }
            if(doc.containsKey("testMode"))
            {
                if(doc["testMode"] == true)
                {
                    if(isSlave)
                    {
                        StatusDb::instance().setSlaveTestMode(true);
                    }
                    else if(isSunshade)
                    {
                        StatusDb::instance().setSunShadeTestMode(true);
                    }
                    ESP_LOGI(TAG, "Registration Msg: Test mode enabled");
                }
                else
                {
                    if(isSlave)
                    {
                        StatusDb::instance().setSlaveTestMode(false);
                    }
                    else if(isSunshade)
                    {
                        StatusDb::instance().setSunShadeTestMode(false);
                    }   
                    ESP_LOGI(TAG, "Registration Msg: Test mode disabled");
                }
            }
            else
            {
                ESP_LOGI(TAG, "Registration Msg does not specify Test mode");
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