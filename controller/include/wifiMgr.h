#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include <string>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>

extern "C" {
    #include "esp_wifi.h"
    #include "esp_wifi_types.h"
}

enum class WifiModes {
    OFF,
    STA,
    AP,
    STA_AP
};
struct WifiConfig {
    std::string ssid = "";
    std::string password = "";
    std::string ipAddress = "";
    std::string gateway = "";
    std::string netmask = "";
    bool isMaster = false;
};

class WifiMgr {
public:

    static WifiMgr& instance();
    void initialize();
    bool isSTAConnected() const;
    bool isAPConnected() const;
    bool isApMode() const;
    static bool isMaster();
    bool mdnsStarted();
    std::string statusText() const;
    std::string currentWifiSSID() const;

    void readHostname(esp_netif_t *sta_netif);

    void startSTA();
    void startAP();
    void startSTAandAP();

private:
    WifiMgr();               // <--- MUST BE PRIVATE
    WifiMgr(const WifiMgr&) = delete;
    WifiMgr& operator=(const WifiMgr&) = delete;
    static TaskHandle_t wifiRetryTaskHandle;
    static void wifi_retry_task(void* param);

    void configureSTA(esp_netif_t *sta_netif);
    void configureAP(esp_netif_t* ap_netif);

    WifiModes determineMode(wifi_event_t event_id);
    void startMdns();
    void configAndStartMdns();
    static void wifi_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    esp_netif_t *sta_netif;
    esp_netif_t *ap_netif;
    WifiConfig STA_config_;
    WifiConfig AP_config_;
    bool sta_connected_;
    bool ap_connected_;
    WifiModes wifiMode_;

};

#endif // WIFI_MGR_H
