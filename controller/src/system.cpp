#include "wsServer.h"
#include "wsServerMgr.h"
#include "WsReceiver.h"
#include "r33SlaveController.h"
#include "wifiMgr.h"
#include "nvsMgr.h"
#include "Pages.h"
#include "LedController.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"


static const char* TAG = "system";
 const char* typeToString(httpd_ws_type_t m) {
    switch (m) {
           case HTTPD_WS_TYPE_CONTINUE:   return "HTTPD_WS_TYPE_CONTINUE";
    case HTTPD_WS_TYPE_TEXT:       return "HTTPD_WS_TYPE_TEXT";
    case HTTPD_WS_TYPE_BINARY:     return "HTTPD_WS_TYPE_BINARY";
    case HTTPD_WS_TYPE_CLOSE:      return "HTTPD_WS_TYPE_CLOSE";
    case HTTPD_WS_TYPE_PING:       return "HTTPD_WS_TYPE_PING";
    case HTTPD_WS_TYPE_PONG:       return "HTTPD_WS_TYPE_PONG";
    default:                      return "UNKNOWN";
    }
 }

extern "C" void system_setup(void)
{
    ESP_LOGI(TAG, "Initializing system...");
    NvsMgr::instance().initialize();
    NvsMgr::instance().dumpNVS();

    LedController::instance().init(GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11);

//    ESP_ERROR_CHECK(nvs_flash_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Event loop creation returned %s", esp_err_to_name(err));
    }    
    WifiMgr& wifiMgr = WifiMgr::instance();
    wifiMgr.initialize();
    wifiMgr.startSTAandAP();

    vTaskDelay(pdMS_TO_TICKS(500)); // Allow WiFi to start before starting the server

    if(wifiMgr.isMaster())
    {
        ESP_LOGW(TAG, "Starting R33 Master Server");
        // 1. Start the WebSocket server (HTTPD)
        WsServer::instance().start();

        // 2. Start the receiver task
        WsReceiver::start(WsServer::instance().incomingQueue());

        // 3. Start the server manager (worker + ping loop)
        WsServerMgr::start();
    }
    else
    {
        ESP_LOGW(TAG, "Starting R33 Slave Client");
        // 1. Start the WebSocket server (HTTPD)
        WsServer::instance().start();

        // 2. Start the receiver task
        WsReceiver::start(WsServer::instance().incomingQueue());

        // 3. Start the server manager (worker + ping loop)
        WsServerMgr::start();

        vTaskDelay(pdMS_TO_TICKS(500)); // Allow WiFi to start before starting the server
        R33SlaveController::instance().begin();

    }
}
