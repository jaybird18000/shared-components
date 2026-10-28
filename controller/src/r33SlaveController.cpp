#include "r33SlaveController.h"
#include "r33ClientController.h"
#include "ACMonitor.h"
#include "GenRelay.h"
#include "wifiMgr.h"
#include "WsServer.h"
#include "Utilities.h"
#include "SharedDataStore.h"
#include "DebugServices.h"
#include "DebugControl.h"
#include "TaskUtilities.h"
#include "ClientsList.h"
#include "DeviceConfig.h"
#include "statusDb.h"
#include "arduinojson.h"
#include "esp_log.h"
#include "esp_random.h"

static ACMonitor acMonitor;
static GenRelay genRelay;

R33SlaveController& R33SlaveController::instance() {
    static R33SlaveController inst;
    return inst;
}

void R33SlaveController::begin() {
    if ((messageTaskHandle)  || (measurementTaskHandle))
    {
        ESP_LOGW(TAG, "Slave controller already running");
        return;
    }

    BaseType_t ok2 = TaskUtilities::createTaskWithQueue(
        TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK,
        &R33SlaveController::messageTaskEntry,
        sizeof(TaskUtilities::MsgItem),
        this
    );

    BaseType_t ok3 = xTaskCreate(
        &R33SlaveController::measurementTaskEntry,
        "r33_slave_measurement_ctrl",
        8192,
        this,
        5,
        &measurementTaskHandle
    );

    if (ok2 != pdPASS) {
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

void R33SlaveController::messageTaskEntry(void* param) {
    auto* self = static_cast<R33SlaveController*>(param);
    self->messageTaskLoop();
}

void R33SlaveController::measurementTaskEntry(void* param) {
    auto* self = static_cast<R33SlaveController*>(param);
    self->measurementTaskLoop();
}



void R33SlaveController::messageTaskLoop() {
    ESP_LOGI(TAG, "Slave controller message loop running");
    while (true) {
        TaskUtilities::MsgItem item{};

        if(TaskUtilities::waitOnQueue(TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK, &item, portMAX_DELAY) == pdTRUE)
        {
            //ESP_LOGI(TAG, "Slave controller message item: %d %d %d %d %d %d %s", item.sockfd, item.fromMaster, item.fromClient, item.fromBrowser, item.len, item.type, item.data);
            if(item.fromMaster && !item.fromBrowser && !item.fromClient)
            {
                ESP_LOGI(TAG, "message rcvd from master, ignoring : %s", item.data);
                
            }
            else if(item.fromClient) {
                ESP_LOGI(TAG, "Error Rcvd message from client: %s", item.data);
            }
            else if(item.fromBrowser) 
            {
                ESP_LOGI(TAG, "Processing message from browser: %s", item.data);
                std::string payload(item.data, item.len);  // local, safe
                handleWebClientMsg(item.sockfd, payload);

            }
            else {
                ESP_LOGW(TAG, "Received message from unknown source: %s", item.data);
            }   
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
            TaskUtilities::MsgItem item{};
            item.fromTask = TaskUtilities::TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK;
            item.toTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
            item.fromMaster = false;
            item.fromClient = true;
            item.fromBrowser = false;
            item.len = acJson.size();
            item.setMsg(acJson);
            auto bytes = item.serialize();
            if(R33ClientController::instance().isClientConnected())
            {
                // send this AC data to the master controller
                R33ClientController::instance().clientSendBinary(bytes.data(), bytes.size());
            }
            else
            {
                //ESP_LOGW(TAG, "NOT sending ACDATA to master");
            }
            
        }
        else
        {
            ESP_LOGW(TAG, "NOT sending ACDATA to master");
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // 2-second interval
    }
}

void R33SlaveController::handleWebClientMsg(int sockfd, const std::string &message)
{

    if (message.find("start_generator") != std::string::npos)
    {
        ESP_LOGW(TAG, "handleWebClientMsg:start_generator command received: Len %d", (int)message.size());

        debugServices::postDebug(std::string("Start generator cmd rcvd "));

        startGenerator();
        return;
    }
    if (message.find("stop_generator") != std::string::npos) {
        ESP_LOGW(TAG, "handleWebClientMsg:stop_generator command received: Len %d", (int)message.size());
        debugServices::postDebug(std::string("Stop generator cmd rcvd "));
        stopGenerator();
        return;
    }
    ESP_LOGW(TAG, "message received from browser, ignoring: %s", message.c_str());
}

void R33SlaveController::startGenerator()
{
    ESP_LOGI(TAG, "startGenerator called:");
    debugServices::postDebug("Slave: startGenerator called");
    genRelay.momentaryStart();
}

void R33SlaveController::stopGenerator()
{
    ESP_LOGI(TAG, "stopGenerator called:");
    debugServices::postDebug("Slave: stopGenerator called");
    genRelay.momentaryStop();
    
}

std::string R33SlaveController::collectACData() {
    // TODO: Replace with real AC monitor readings
        // Read current values
    ACMonitor::ACResults voltageResults = acMonitor.readVoltage();
    ACMonitor::ACResults currentResults = acMonitor.readCurrent();
    float voltage = voltageResults.rms_voltage;
    float current = currentResults.rms_current;
    float freq = voltageResults.frequency;
//    ESP_LOGI(TAG,"freq = %0.1f",freq);
    float watts = 10.0f;
    if(DebugControl::testMode())
    {
        switch(genRelay.state())
        {
            case GenRelay::STARTING:
                testModeAcMonitorStartStopDelayCounter++;
                generateGoodACData = false;
                if(testModeAcMonitorStartStopDelayCounter >= 2)
                {
                    generateGoodACData = true;
                }
                break;
            case GenRelay::RUNNING:
                testModeAcMonitorStartStopDelayCounter = 0;
                generateGoodACData = true;
                break;
            case GenRelay::STOPPING:
                testModeAcMonitorStartStopDelayCounter++;
                generateGoodACData = true;
                if(testModeAcMonitorStartStopDelayCounter >= 2)
                {
                    generateGoodACData = false;
                }
                break;
            case GenRelay::NOT_RUNNING:
                testModeAcMonitorStartStopDelayCounter = 0;
                generateGoodACData = false;
                break;
            default:
                generateGoodACData = false;
                break;
        }
        if(generateGoodACData)
        {
            int peak2peakNoise = 10;
            int currentNoiseScaler = 30;
            float nominalVoltage = 120.0f;
            float nominalCurrent = 7.0f;
            uint32_t r = esp_random() % (peak2peakNoise + 1);
            // center the noise around zero, then add a small fractional component
            float noise = float(r) - (float(peak2peakNoise) / 2.0) + (float(r) / float(peak2peakNoise));
            //ESP_LOGI(TAG, "TestMode genState STARTING: r=%d noise=%0.2f ", r, noise);
            voltage = nominalVoltage + noise;

            current = nominalCurrent + (noise / currentNoiseScaler);            

            freq = 60.0f;     // Example test frequency
            watts = voltage * current; // Example test watts calculation
//            ESP_LOGI(TAG, "TestMode genState STARTING Setting: AC Data: voltage=%0.2f current=%0.2f freq=%0.1f", voltage, current, freq);
        }
        else
        {
            voltage = 0.0f; // Example test voltage when stopped
            current = 0.0f; // Example test current when stopped
            freq = 0.0f;    // Example test frequency when stopped
            watts = 0.0f;   // Example test watts calculation when stopped
//            ESP_LOGI(TAG, "TestMode genState STOPPING/NOT_RUNNING Setting: AC Data: voltage=%0.2f current=%0.2f freq=%0.1f", voltage, current, freq);
        }
    }
    genRelay.setState(voltage);

//    ESP_LOGI(TAG, "Collected voltage %0.2f  current %0.2f", voltage, current);
    std::string genStatus = genRelay.stateText();

    // collectACData runs on the client(slave) side,
    // storing the acData in the SharedDataStore 
    // allows the slave web browser page to show the latest AC data even if the master is not connected, 
    // by reading from the SharedDataStore. The SharedDataStore is updated with the latest AC data every time collectACData is called, 
    // which is every 2 seconds in the measurementTaskLoop. 
    // This way, the slave can display real-time AC data on its web page regardless of the master's connection status.
    SharedDataStore::set(voltage, current, freq, watts, genStatus.c_str());
    StatusDb::instance().setAcData(voltage, current, freq, watts, genRelay.state());
    
    // prepare a json message to send this updated info the master controller via WebSocket
    DynamicJsonDocument doc(256);

    doc["type"] = "ac_update";
    doc["source"] = "slave";

    JsonObject p = doc["payload"].to<JsonObject>();
    p["voltage"] = voltage;
    p["current"] = current;
    p["frequency"] = freq;
    p["watts"] = watts;
//    p["genStatus"] = genStatus;
    p["genStatus"] = static_cast<int>(genRelay.state());

    std::string out;
    serializeJson(doc, out);

    return out;
}