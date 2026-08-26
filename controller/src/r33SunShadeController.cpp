#include "r33SunShadeController.h"
//#include "ACMonitor.h"
//#include "GenRelay.h"
#include "wifiMgr.h"
#include "WsServer.h"
#include "Utilities.h"
#include "SharedDataStore.h"
#include "TaskUtilities.h"
#include "DebugServices.h"
#include "DebugControl.h"
#include "arduinojson.h"
#include "esp_log.h"

//static ACMonitor acMonitor;
//static GenRelay genRelay;

R33SunShadeController& R33SunShadeController::instance() {
    static R33SunShadeController inst;
    return inst;
}

void R33SunShadeController::begin() {
    if ((messageTaskHandle)  || (measurementTaskHandle))
    {
        ESP_LOGW(TAG, "SunShade controller already running");
        return;
    }

    BaseType_t ok2 = TaskUtilities::createTaskWithQueue(
        TaskUtilities::TaskNames::R33_SUNSHADE_CONTROLLER_MESSAGE_TASK,
        &R33SunShadeController::messageTaskEntry,
        sizeof(TaskUtilities::MsgItem),
        this
    );
    
    BaseType_t ok3 = xTaskCreate(
        &R33SunShadeController::measurementTaskEntry,
        "r33_slave_measurement_ctrl",
        8192,
        this,
        5,
        &measurementTaskHandle
    );

    if (ok2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sunshade controller message task");
        messageTaskHandle= nullptr;
    } 
    else if (ok3 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sunshade controller measurement task");
        measurementTaskHandle = nullptr;
    } 
    else 
    {
        ESP_LOGI(TAG, "Sunshade controller task started");
    }

}

void R33SunShadeController::messageTaskEntry(void* param) {
    auto* self = static_cast<R33SunShadeController*>(param);
    self->messageTaskLoop();
}

void R33SunShadeController::measurementTaskEntry(void* param) {
    auto* self = static_cast<R33SunShadeController*>(param);
    self->measurementTaskLoop();
}



void R33SunShadeController::messageTaskLoop() {
    ESP_LOGI(TAG, "SunShade controller message loop running");
    while (true) {
        TaskUtilities::MsgItem item{};

        if(TaskUtilities::waitOnQueue(TaskUtilities::TaskNames::R33_SUNSHADE_CONTROLLER_MESSAGE_TASK, &item, portMAX_DELAY) == pdTRUE)
        {
           //ESP_LOGI(TAG, "Slave controller message item: %d %d %d %d %d %d %s", item.sockfd, item.fromMaster, item.fromClient, item.fromBrowser, item.len, item.type, item.data);
            if(item.fromMaster && !item.fromBrowser && !item.fromClient)
            {
                ESP_LOGI(TAG, "message rcvd from master, ignoring : %s", item.data);

            }
            else if(item.fromClient) {
                ESP_LOGI(TAG, "Error Rcvd message from client: %s", item.data);
            }
            else if(item.fromBrowser) {
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

void R33SunShadeController::measurementTaskLoop() {
    ESP_LOGI(TAG, "SunShade controller measurement loop running");
    while (true) {

//        static int numSent = 0;
//        if(numSent < 10)
        if(true)
        {
//            numSent++;
//            std::string acJson = collectACData();
//            if (R33ClientController::instance().isClientConnected()) {
//                bool ok = R33ClientController::instance().clientSend(acJson);
//            }

        }
        else
        {
            ESP_LOGW(TAG, "NOT sending ??? to master");
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // 2-second interval
    }
}

void R33SunShadeController::handleWebClientMsg(int sockfd, const std::string &message)
{

    if (message.find("start_generator") != std::string::npos)
    {
        ESP_LOGW(TAG, "handleWebClientMsg:start_generator command received: Len %d", (int)message.size());

        debugServices::postDebug(std::string("Start generator cmd rcvd "));

        //startGenerator();
        return;
    }
    if (message.find("stop_generator") != std::string::npos) {
        ESP_LOGW(TAG, "handleWebClientMsg:stop_generator command received: Len %d", (int)message.size());
        debugServices::postDebug(std::string("Stop generator cmd rcvd "));
        //stopGenerator();
        return;
    }
    ESP_LOGW(TAG, "message received from browser, ignoring: %s", message.c_str());
}