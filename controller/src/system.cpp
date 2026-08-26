#include "wsServer.h"
#include "wsServerMgr.h"
#include "r33MasterController.h"
#include "r33SlaveController.h"
#include "r33SunShadeController.h"
#include "r33ClientController.h"
#include "BrowserController.h"
#include "wifiMgr.h"
#include "nvsMgr.h"
#include "Pages.h"
#include "LedController.h"
#include "DeviceConfig.h"
#include "DebugServices.h"
#include "DebugControl.h"
#include "ConsoleApp.h"
#include "SharedDataStore.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"



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
    ESP_LOGI(TAG, "Initializing system");

    DebugControl::setAll(false);

    debugServices::init();

    NvsMgr::instance().initialize();
    NvsMgr::instance().dumpNVS();
    debugServices::postDebug("Booting system...");

    LedController::instance().init(GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11);

//    ESP_ERROR_CHECK(nvs_flash_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Event loop creation returned %s", esp_err_to_name(err));
    }    
    WifiMgr& wifiMgr = WifiMgr::instance();
    wifiMgr.initialize();
    wifiMgr.startSTAandAP();
    SharedDataStore::init();
    vTaskDelay(pdMS_TO_TICKS(500)); // Allow WiFi to start before starting the server
    vTaskDelay(pdMS_TO_TICKS(5000)); // Allow WiFi to start before starting the server
    // used for entering commands via keyboard useing serial monitor
    ConsoleApp& console = ConsoleApp::instance();
    // 1. Start the WebSocket server (HTTPD)
    WsServer::instance().start();
    // 2. Start the server manager (worker + ping loop)
    wsServerMgr::instance().start();
    // 3. Start the browser manager (worker + ping loop)
    BrowserController::instance().begin();

    if(DeviceConfig::instance().isMasterDevice())
    {
        // must configure console output to not go to both usb connectors
        // use menuconfig and search for usb. find ESP-STDIO 
        // Channel for console output, set it to usb serial/jtag controller. 
        // This will prevent the console output from going to both usb connectors and causing a crash.
        console.start_console();
        ESP_LOGW(TAG, "Starting R33 Master Server");

        r33MasterController::start();
    }
    else if (DeviceConfig::instance().isSunShadeDevice())
    {
        ESP_LOGW(TAG, "Starting R33 Sunshade Client");

        R33ClientController::instance().startMessageTask();
        vTaskDelay(pdMS_TO_TICKS(500));
        R33ClientController::instance().startSocketTask();
        vTaskDelay(pdMS_TO_TICKS(500)); // Allow WiFi to start before starting the server
        R33SunShadeController::instance().begin();
    }
    else
    {
        // must configure console output to not go to both usb connectors
        // use menuconfig and search for usb. find ESP-STDIO 
        // Channel for console output, set it to usb serial/jtag controller. 
        // This will prevent the console output from going to both usb connectors and causing a crash.
        console.start_console();
        ESP_LOGW(TAG, "Starting R33 Slave Client");

        R33ClientController::instance().startMessageTask();
        vTaskDelay(pdMS_TO_TICKS(500));
        R33ClientController::instance().startSocketTask();

        vTaskDelay(pdMS_TO_TICKS(500)); // Allow WiFi to start before starting the server
        R33SlaveController::instance().begin();

    }
    bool isSunShade = DeviceConfig::instance().isSunShadeDevice();
    ESP_LOGW(TAG, "Is sunshade device: %s", isSunShade ? "true" : "false");
    debugServices::postDebug("System setup complete. Device role: " + std::string(isSunShade ? "SUNSHADE" : (DeviceConfig::instance().isMasterDevice() ? "MASTER" : "SLAVE")));
}
