#include "wifiMgr.h"
#include "nvsMgr.h"
#include "LedController.h"
#include "WsServer.h"
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include "lwip/etharp.h"
#include <esp_wifi.h>
#include <esp_mac.h>
#include "mdns.h"
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>

static const char* TAG = "wifiMgr";
static const char* NVS_NAMESPACE = "wifi_cfg";
static WifiMgr* s_instance = nullptr;
static bool mdnsStartedSta = false;
static bool mdnsStartedAp = false;
static bool isMaster_ = false;
TaskHandle_t WifiMgr::wifiRetryTaskHandle = nullptr;
//static NvsMgr& nvsMgr = NvsMgr::instance();

WifiMgr::WifiMgr()
    : sta_connected_(false), ap_connected_(false), wifiMode_(WifiModes::OFF)
{
    s_instance = this;
    wifiRetryTaskHandle = nullptr;
    STA_config_ = NvsMgr::instance().currentSTA_Config();
    AP_config_ = NvsMgr::instance().currentAP_Config(); 
    // uncommetn these to hardcode a wifi ssid 
    //  STA_config_.ssid = "MySpectrumWiFi17-2G";
    // STA_config_.password = "townengine498"; 
    //STA_config_.ssid = "GL-SFT1200-868";
    //STA_config_.password = "goodlife";
    // AP_config_.ssid = "ESP32-AP";
    // AP_config_.password = "12345678";
    ESP_LOGI(TAG, "wifiMgr constructor currentSTA_Config() STA SSID: %s  PASS: %s", STA_config_.ssid.c_str(), STA_config_.password.c_str());
    ESP_LOGI(TAG, "wifiMgr constructor currentAP_Config() AP SSID: %s  PASS: %s ip: %s gw: %s nm: %s", AP_config_.ssid.c_str(), AP_config_.password.c_str(), AP_config_.ipAddress.c_str(), AP_config_.gateway.c_str(), AP_config_.netmask.c_str());



    // determine whether we are the master or slave by reading mac address
    uint8_t mac[6];
    uint8_t slaveMac[6] = {0x3C, 0xDC, 0x75, 0x71, 0x52, 0x3C}; // Mac for slave device
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    ESP_LOGI("MAC", "STA MAC = %02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    isMaster_ = false;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != slaveMac[i]) {
            ESP_LOGI("MAC", "This is the MASTER device");
            isMaster_ = true;
            break;
        }
        if (i == 5) {
            ESP_LOGI("MAC", "This is the SLAVE device");

        }
    }

}

WifiMgr& WifiMgr::instance()
{
//    ESP_LOGI(TAG, "wifiMgr instance called -----------------------");
    static WifiMgr instance;
    return instance;
}

bool WifiMgr::isSTAConnected() const
{
    return sta_connected_;
}

bool WifiMgr::isAPConnected() const
{
    return ap_connected_;
}

bool WifiMgr::isApMode() const
{
    if(wifiMode_ == WifiModes::AP) {
        return true;
    }
    return false;
}

bool WifiMgr::isMaster()
{
    return isMaster_;
}

std::string WifiMgr::statusText() const
{
    if (sta_connected_) {
        return "station connected";
    }
    if (isApMode()) {
        return "access point";
    }
    if (!STA_config_.ssid.empty()) {
        return "station connecting";
    }
    return "ap fallback";
}

std::string WifiMgr::currentWifiSSID() const
{
    if (sta_connected_ || wifiMode_ == WifiModes::STA || wifiMode_ == WifiModes::STA_AP) {
        return STA_config_.ssid;
    }
    if (ap_connected_ || wifiMode_ == WifiModes::AP) {
        return AP_config_.ssid;
    }
    return "";
}

void WifiMgr::readHostname(esp_netif_t *sta_netif)
{
    const char* hostname = NULL;
    esp_err_t err = esp_netif_get_hostname(sta_netif, &hostname);

    if (err == ESP_OK && hostname != NULL) {
        ESP_LOGI("NETIF", "STA hostname: %s", hostname);
    } else {
        ESP_LOGE("NETIF", "Failed to read hostname: %s", esp_err_to_name(err));
    }
}

void WifiMgr::configureSTA(esp_netif_t *sta_netif)
{
    wifi_config_t sta_config = {};
    strncpy(reinterpret_cast<char*>(sta_config.sta.ssid), STA_config_.ssid.c_str(), sizeof(sta_config.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(sta_config.sta.password), STA_config_.password.c_str(), sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // the next two setting should work, but my simple wifi did not set them
//    sta_config.sta.pmf_cfg.capable = true;
//    sta_config.sta.pmf_cfg.required = false;

    esp_wifi_set_config(WIFI_IF_STA, &sta_config);

}

void WifiMgr::configureAP(esp_netif_t* ap_netif)
{

    // --- Set custom AP IP address ---
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr      = esp_ip4addr_aton(AP_config_.ipAddress.c_str());
    ip_info.gw.addr      = esp_ip4addr_aton(AP_config_.gateway.c_str());
    ip_info.netmask.addr = esp_ip4addr_aton(AP_config_.netmask.c_str());

    // Stop DHCP server before changing IP
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    wifi_config_t ap_config = {};

    strncpy((char *)ap_config.ap.ssid, AP_config_.ssid.c_str(), sizeof(ap_config.ap.ssid) - 1);
    strncpy((char *)ap_config.ap.password, AP_config_.password.c_str(), sizeof(ap_config.ap.password) - 1);
    ap_config.ap.ssid_len = strlen(AP_config_.ssid.c_str());
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (strlen(AP_config_.password.c_str()) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    wifiMode_ = WifiModes::AP;
    ap_connected_ = false;
}

void WifiMgr::startSTA()
{
    ESP_LOGI(TAG, "WiFi starting STA mode only");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    configureSTA(sta_netif);

    ESP_ERROR_CHECK(esp_wifi_start());
    // ⭐ Disable WiFi power save — CRITICAL FIX ⭐
    esp_wifi_set_ps(WIFI_PS_NONE);
    // STA connect happens automatically via WIFI_EVENT_STA_START
    wifiMode_ = WifiModes::STA;
    sta_connected_ = false;    
    ESP_LOGI(TAG, "WiFi STA started");
    ESP_LOGI(TAG, "STA SSID: %s", STA_config_.ssid.c_str()); 
}

void WifiMgr::startAP()
{
    ESP_LOGI(TAG, "WiFi starting AP mode only");    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    configureAP(ap_netif);
  
    wifiMode_ = WifiModes::AP;
    ap_connected_ = false;
    ESP_ERROR_CHECK(esp_wifi_start());
    // ⭐ Disable WiFi power save — CRITICAL FIX ⭐
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "WiFi AP started");
    ESP_LOGI(TAG, "AP SSID: %s  PASS: %s", AP_config_.ssid.c_str(), AP_config_.password.c_str());

}
void WifiMgr::startSTAandAP()
{
    ESP_LOGI(TAG, "WiFi starting AP+STA mode only");
    // Enable AP + STA concurrently
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    configureSTA(sta_netif);
    configureAP(ap_netif);
    wifiMode_ = WifiModes::STA_AP;
    ESP_ERROR_CHECK(esp_wifi_start());
    // ⭐ Disable WiFi power save — CRITICAL FIX ⭐
    esp_wifi_set_ps(WIFI_PS_NONE);    
    // STA connect happens automatically via WIFI_EVENT_STA_START
    ESP_LOGI(TAG, "WiFi AP+STA started");
    ESP_LOGI(TAG, "AP SSID: %s  PASS: %s", AP_config_.ssid.c_str(), AP_config_.password.c_str());
    ESP_LOGI(TAG, "STA SSID: %s", STA_config_.ssid.c_str());
    readHostname(sta_netif);
    // STA connect happens automatically via WIFI_EVENT_STA_START
    
    // create retry task
    xTaskCreate(wifi_retry_task, "wifi_retry_task", 4096, NULL, 5, &wifiRetryTaskHandle);
}
void WifiMgr::initialize()
{
    ESP_ERROR_CHECK(esp_netif_init());
  
    // Create BOTH interfaces
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif  = esp_netif_create_default_wifi_ap();


    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, this, NULL));

    ESP_LOGI(TAG, "wifiMgr initialized");

    
}

void WifiMgr::wifi_retry_task(void* param)
{
    uint32_t delayMs = 4000;  // start with 4 seconds

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // wait for signal

        vTaskDelay(pdMS_TO_TICKS(delayMs));

        ESP_LOGI("WIFI", "Retrying STA connection...");
        esp_wifi_connect();

        // Exponential backoff up to 30 seconds
        if (delayMs < 30000) delayMs += 2000;
    }
}

WifiModes WifiMgr::determineMode(wifi_event_t event_id)
{
    WifiModes theMode = WifiModes::STA_AP;
    switch (event_id) {
        case WIFI_EVENT_STA_CONNECTED:
            switch (wifiMode_) {
                case WifiModes::STA_AP:
                    theMode =  WifiModes::STA;  // turn off ap mode when sta successfully connects
                    break;                
                default:
                    break;  
            }
            break;
        default:
            theMode =  WifiModes::STA_AP;
            break;
    }
    return theMode;
}

bool WifiMgr::mdnsStarted()
{
    bool result = false;
    if(mdnsStartedSta || mdnsStartedAp)
    {
        result = true;
    }
    return result;
}
void WifiMgr::startMdns()
{   
    // this routine can be called in either sta or ap mode
    // we must set the correct started status base on the mode
    ESP_LOGW(TAG,"startMdns called %d", instance().wifiMode_);
    if((instance().wifiMode_ == WifiModes::STA) && !mdnsStartedSta)
    {
        configAndStartMdns();
        mdnsStartedSta = true;
    }
    else if((instance().wifiMode_ == WifiModes::AP) && !mdnsStartedAp)
    {
        configAndStartMdns();
        mdnsStartedAp = true;
    }
    else if((instance().wifiMode_ == WifiModes::STA_AP) && !mdnsStartedAp)
    {
        configAndStartMdns();
        mdnsStartedAp = true;
    }
  
}

void WifiMgr::configAndStartMdns()
{
        // how to store and retrive the hostname from nvs
    // nvs_set_str(handle, "mdns_host", "r33-esp32-slave");
    // char hostname[32];
    // size_t len = sizeof(hostname);
    // nvs_get_str(handle, "mdns_host", hostname, &len);
    // mdns_hostname_set(hostname);
    mdns_init();
    if(!isMaster_)
    {
        mdns_hostname_set("r33-slave");
        mdns_instance_name_set("R33 Slave Controller");
        mdns_service_add("R33 Slave Controller", "_http", "_tcp", 80, NULL, 0);
        ESP_LOGW(TAG,"Setting hostname: r33-slave and starting mdns advertisements");
    }
    else
    {
        mdns_hostname_set("r33-master");
        mdns_instance_name_set("R33 Master Controller");
        mdns_service_add("R33 Master Controller", "_http", "_tcp", 80, NULL, 0);
        ESP_LOGW(TAG,"Setting hostname: r33-master and starting mdns advertisements");
    }  
}

void WifiMgr::wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WifiMgr* instance = static_cast<WifiMgr*>(event_handler_arg);
    wifi_event_t event_enum = static_cast<wifi_event_t>(event_id);
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA start, connecting to STA %s", instance->STA_config_.ssid.c_str());
            LedController::instance().off(1);
            LedController::instance().stopFlash();
            LedController::instance().flash(1,400,600);            
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA connected to SSID %s", instance->STA_config_.ssid.c_str());
            WsServer::instance().postDebug("STA connected to SSID " + instance->STA_config_.ssid);
            LedController::instance().off(1);
            LedController::instance().stopFlash();
            LedController::instance().flash(1,800,200);
            if(instance->wifiMode_ == WifiModes::STA_AP) {
                ESP_LOGI(TAG, "Stop AP mode WiFi");
                esp_wifi_set_mode(WIFI_MODE_STA);   // turn off AP mode when STA successfully connects 
            }

            instance->wifiMode_ = instance->determineMode(event_enum);
            instance->sta_connected_ = true;
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            instance->sta_connected_ = false;
            instance->wifiMode_ = instance->determineMode(event_enum);
            mdnsStartedSta = false;
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGI(TAG, "STA disconnected: reason=%d", event->reason);
            WsServer::instance().postDebug("STA disconnected: reason=" + std::to_string(event->reason));
            LedController::instance().off(1);
            LedController::instance().stopFlash();
            LedController::instance().flash(1,200,800);

            // Ignore disconnects caused by esp_wifi_stop()
            if (event->reason == WIFI_REASON_ASSOC_LEAVE ||
                event->reason == WIFI_REASON_UNSPECIFIED) {
                ESP_LOGI("WIFI", "Ignoring intentional disconnect");
                return;
            }
            esp_wifi_set_mode(WIFI_MODE_APSTA); // turn AP back on
             // Signal retry task (non-blocking)
            xTaskNotifyGive(wifiRetryTaskHandle); 
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Station connected: MAC=%02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                     event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
            instance->wifiMode_ = instance->determineMode(event_enum);
            instance->ap_connected_ = true;
            instance->startMdns();
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Station disconnected: MAC=%02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                     event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
            instance->wifiMode_ = instance->determineMode(event_enum);
            instance->ap_connected_ = false;
            mdnsStartedAp = false;
            break;
        }
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));

        instance->wifiMode_ = instance->determineMode(event_enum);
        instance->sta_connected_ = true;
        ESP_LOGI(TAG, "STA got IP: %s wifiMode_:%d ", ip_str, instance->wifiMode_);
        WsServer::instance().postDebug("STA got IP: %s wifiMode_:%d ", ip_str, instance->wifiMode_);
        LedController::instance().off(1); 
        LedController::instance().stopFlash();
        LedController::instance().on(1);
        esp_netif_ip_info_t ip;
        esp_netif_get_ip_info(instance->sta_netif, &ip);

        // ⭐ Force ARP refresh by re‑setting the IP info
        esp_netif_set_ip_info(instance->sta_netif, &ip);

        // ⭐ 2. Delay so iPhone doesn't connect too early
        vTaskDelay(pdMS_TO_TICKS(400));

        instance->startMdns();
        WsServer::instance().postDebug("Starting mDNS");
    }    

}
