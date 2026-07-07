#include "nvsMgr.h"
#include "wifiMgr.h"
#include <nvs_flash.h>
#include <nvs.h>
#include "esp_log.h"
#include <string.h>


static const char* TAG = "nvsMgr";
static const char* TAG_NVS_DUMP = "NVS_DUMP";
static const char* NVS_NAMESPACE = "wifi_cfg";
//static const char* NVS_NAMESPACE = "temp";
static const char* NVS_STA_SSID_KEY = "STA_ssid";
static const char* NVS_STA_PASSWORD_KEY = "STA_password";
static const char* NVS_AP_SSID_KEY = "AP_ssid";
static const char* NVS_AP_PASSWORD_KEY = "AP_password";
static const char* NVS_AP_IP_ADDRESS_KEY = "AP_ipAddress";
static const char* NVS_AP_GATEWAY_KEY = "AP_gateway";
static const char* NVS_AP_NETMASK_KEY = "AP_netmask";
static const char* DEFAULT_STA_SSID = "Not Configured";
static const char* DEFAULT_STA_PASSWORD = "Not Configured";
static const char* DEFAULT_AP_SSID = "ESP32-AP";
static const char* DEFAULT_AP_PASSWORD = "12345678";
static const char* DEFAULT_AP_IP_ADDRESS = "192.168.4.1";
static const char* DEFAULT_AP_GATEWAY = "192.168.4.1";
static const char* DEFAULT_AP_NETMASK = "255.255.255.0";

NvsMgr::NvsMgr()
{
    // STA_config_.ssid = "MySpectrumWiFi17-2G";
    // STA_config_.password = "townengine498";
    AP_config_.ssid = "";
    AP_config_.password = "";
}

NvsMgr &NvsMgr::instance()
{
    static NvsMgr instance;
    return instance;
}

void NvsMgr::initialize()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_LOGI(TAG, "NVS FLASH ERROR, erased and re-initializing");
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS FLASH ERROR, unable to initialize: %s", esp_err_to_name(err));
        return;
    }
     ESP_LOGI(TAG, "STA SSID: %s  PASS: %s", STA_config_.ssid.c_str(), STA_config_.password.c_str());
    ESP_LOGI(TAG, "AP SSID: %s  PASS: %s  IP: %s  GATEWAY: %s  NETMASK: %s", 
        AP_config_.ssid.c_str(), AP_config_.password.c_str(), AP_config_.ipAddress.c_str(), 
        AP_config_.gateway.c_str(), AP_config_.netmask.c_str());   
    loadSTA_config();
    loadAP_config();
    ESP_LOGI(TAG, "NVS FLASH Initialized");
    #if 1
    ESP_LOGI(TAG, "NVS FLASH Initialized, testing read of existing config...");

    ESP_LOGI(TAG, "STA SSID: %s  PASS: %s", STA_config_.ssid.c_str(), STA_config_.password.c_str());
    ESP_LOGI(TAG, "AP SSID: %s  PASS: %s  IP: %s  GATEWAY: %s  NETMASK: %s", 
        AP_config_.ssid.c_str(), AP_config_.password.c_str(), AP_config_.ipAddress.c_str(), 
        AP_config_.gateway.c_str(), AP_config_.netmask.c_str());
    // STA_config_.ssid = "MySpectrumWiFi17-2G";
    // STA_config_.password = "townengine498";
    // AP_config_.ssid = "ESP32-AP";
    // AP_config_.password = "12345678"; 
    ESP_LOGI(TAG, "NVS FLASH Initialized, finished testing read of existing config, now saving default config...");  
    #endif 
}
void NvsMgr::saveAP_Config(const std::string &ssid, 
                            const std::string &password,
                            const std::string &ipAddress,
                            const std::string &gateway,
                            const std::string &netmask)
{

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_str(handle, "AP_ssid", ssid.c_str());
    nvs_set_str(handle, "AP_password", password.c_str());
    nvs_set_str(handle, "AP_ipAddress", ipAddress.c_str());
    nvs_set_str(handle, "AP_gateway", gateway.c_str()); 
    nvs_set_str(handle, "AP_netmask", netmask.c_str());
    nvs_commit(handle);
    nvs_close(handle);

    AP_config_.ssid = ssid;
    AP_config_.password = password;
    AP_config_.ipAddress = ipAddress;
    AP_config_.gateway = gateway;
    AP_config_.netmask = netmask;

}

void NvsMgr::saveAll_Config(const std::string &sta_ssid, const std::string &sta_password, 
                            const std::string &ap_ssid, const std::string &ap_password, 
                            const std::string &ap_ipAddress, const std::string &ap_gateway, const std::string &ap_netmask)
{
    saveSTA_Config(sta_ssid, sta_password);
    saveAP_Config(ap_ssid, ap_password, ap_ipAddress, ap_gateway, ap_netmask);
}

void NvsMgr::saveSTA_Config(const std::string& ssid, const std::string& password)
{

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_str(handle, NVS_STA_SSID_KEY, ssid.c_str());
    nvs_set_str(handle, NVS_STA_PASSWORD_KEY, password.c_str());
    nvs_commit(handle);
    nvs_close(handle);

    STA_config_.ssid = ssid;
    STA_config_.password = password;

}


WifiConfig NvsMgr::currentSTA_Config() const
{
    ESP_LOGI(TAG, "nvsMgr currentSTA_Config() STA SSID: %s  PASS: %s", STA_config_.ssid.c_str(), STA_config_.password.c_str());
    return STA_config_;
}

WifiConfig NvsMgr::currentAP_Config() const
{
    ESP_LOGI(TAG, "nvsMgr currentAP_Config() AP SSID: %s  PASS: %s  IP: %s  GATEWAY: %s  NETMASK: %s", 
        AP_config_.ssid.c_str(), AP_config_.password.c_str(), AP_config_.ipAddress.c_str(), 
        AP_config_.gateway.c_str(), AP_config_.netmask.c_str());
    return AP_config_;
}

void NvsMgr::loadSTA_config()
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGI(TAG, "Did not find NVS_NAMESPACE %s for STA using default", NVS_NAMESPACE);
        STA_config_.ssid = DEFAULT_STA_SSID;
        STA_config_.password = DEFAULT_STA_PASSWORD;
        return;
    }
    size_t length = 0;
    if (nvs_get_str(handle, NVS_STA_SSID_KEY, nullptr, &length) == ESP_OK && length > 1) {
        char* ssid = static_cast<char*>(malloc(length));
        if (ssid) {
            nvs_get_str(handle, NVS_STA_SSID_KEY, ssid, &length);
            STA_config_.ssid = ssid;
            free(ssid);
        }
        else        {
            STA_config_.ssid = DEFAULT_STA_SSID;
        }
    }
    else
    {
        STA_config_.ssid = DEFAULT_STA_SSID;
    }
    length = 0;
    if (nvs_get_str(handle, NVS_STA_PASSWORD_KEY, nullptr, &length) == ESP_OK && length > 1) {
        char* password = static_cast<char*>(malloc(length));
        if (password) {
            nvs_get_str(handle, NVS_STA_PASSWORD_KEY, password, &length);
            STA_config_.password = password;
            free(password);
        }
        else
        {
            STA_config_.password = DEFAULT_STA_PASSWORD;
        }
    }
    else
    {
        STA_config_.password = DEFAULT_STA_PASSWORD;
    }
    nvs_close(handle);
}
void NvsMgr::loadAP_config()
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGI(TAG, "Did not find NVS_NAMESPACE %s for AP, using default", NVS_NAMESPACE);
        AP_config_.ssid = DEFAULT_AP_SSID;
        AP_config_.password = DEFAULT_AP_PASSWORD;
        AP_config_.ipAddress = DEFAULT_AP_IP_ADDRESS;
        AP_config_.gateway = DEFAULT_AP_GATEWAY;
        AP_config_.netmask = DEFAULT_AP_NETMASK;
        return;
    }
    ESP_LOGI(TAG, "getting ap ssid. ");

    size_t length = 0;
    if (nvs_get_str(handle, NVS_AP_SSID_KEY, nullptr, &length) == ESP_OK && length > 1) {
        char* ssid = static_cast<char*>(malloc(length));
        if (ssid) {
            nvs_get_str(handle, NVS_AP_SSID_KEY, ssid, &length);
            AP_config_.ssid = ssid;
            free(ssid);
        }
        else
        {
            AP_config_.ssid = DEFAULT_AP_SSID;
        }
    }
    else
    {
        AP_config_.ssid = DEFAULT_AP_SSID;
    }
    ESP_LOGI(TAG, "getting ap pw. ");
    length = 0;
    if (nvs_get_str(handle, NVS_AP_PASSWORD_KEY, nullptr, &length) == ESP_OK && length > 1) {
        char* password = static_cast<char*>(malloc(length));
        if (password) {
            nvs_get_str(handle, NVS_AP_PASSWORD_KEY, password, &length);
            AP_config_.password = password;
            free(password);
        }
        else
        {
            AP_config_.password = DEFAULT_AP_PASSWORD;
        }
    }
    else
    {
        AP_config_.password = DEFAULT_AP_PASSWORD;
    }
    ESP_LOGI(TAG, "getting ap ip. ");
    length = 0;
    if (nvs_get_str(handle, NVS_AP_IP_ADDRESS_KEY, nullptr, &length) == ESP_OK && length > 1) {
        char* ipAddress = static_cast<char*>(malloc(length));
        if (ipAddress) {
                  ESP_LOGI(TAG, "found apmode ip address. %s", ipAddress);
            nvs_get_str(handle, NVS_AP_IP_ADDRESS_KEY, ipAddress, &length);
            AP_config_.ipAddress = ipAddress;
            free(ipAddress);
        }
        else
        {
                  ESP_LOGI(TAG, "did not find apmode ip address setting ip address. %s", DEFAULT_AP_IP_ADDRESS);
            AP_config_.ipAddress = DEFAULT_AP_IP_ADDRESS;
        }
    }
    else
    {
              ESP_LOGI(TAG, "did not find apmode ip address setting ip address. %s", DEFAULT_AP_IP_ADDRESS);
        AP_config_.ipAddress = DEFAULT_AP_IP_ADDRESS;
    }
    length = 0;
    if (nvs_get_str(handle, NVS_AP_GATEWAY_KEY, nullptr, &length) == ESP_OK && length > 1) {
        char* gateway = static_cast<char*>(malloc(length));
        if (gateway) {
            nvs_get_str(handle, NVS_AP_GATEWAY_KEY, gateway, &length);
            AP_config_.gateway = gateway;
            free(gateway);
        }
        else
        {
            AP_config_.gateway = DEFAULT_AP_GATEWAY;
        }
    }
    else
    {
        AP_config_.gateway = DEFAULT_AP_GATEWAY;
    }
    length = 0;
    if (nvs_get_str(handle, NVS_AP_NETMASK_KEY, nullptr, &length) == ESP_OK && length > 1) {
        char* netmask = static_cast<char*>(malloc(length));
        if (netmask) {
            nvs_get_str(handle, NVS_AP_NETMASK_KEY, netmask, &length);
            AP_config_.netmask = netmask;
            free(netmask);
        }
        else
        {
            AP_config_.netmask = DEFAULT_AP_NETMASK;
        }
    }
    else
    {
        AP_config_.netmask = DEFAULT_AP_NETMASK;
    }
    nvs_close(handle);
}

void NvsMgr::loadAll_config()
{
    loadSTA_config();
    loadAP_config();    
}

void NvsMgr::dumpNVS()
{
    nvs_iterator_t it = nullptr;
    esp_err_t err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG_NVS_DUMP, "No NVS entries found.");
        return;
    }

    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        ESP_LOGI(TAG_NVS_DUMP,
                 "Namespace: %-12s  Key: %-16s  Type: %d",
                 info.namespace_name,
                 info.key,
                 info.type);

        err = nvs_entry_next(&it);
    }
}
