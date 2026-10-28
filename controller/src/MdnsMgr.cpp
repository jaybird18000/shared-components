#include "MdnsMgr.h"
#include "DebugControl.h"
#include <lwip/inet.h>
#include "esp_log.h"
#include "esp_netif.h"
#include <cstring>
#include <algorithm>

static const char* TAG = "MDNS_MGR";
static constexpr TickType_t MDNS_RESPONDER_PERIOD = pdMS_TO_TICKS(10000);
static constexpr uint32_t MDNS_RESPONDER_QUERY_TIMEOUT = 3000;
static constexpr int MDNS_RESPONDER_MAX_RESULTS = 20;

namespace 
{
    std::string normalizeHostname(const std::string& hostname)
    {
        std::string normalized = hostname;
        if (normalized.size() > 6 && normalized.compare(normalized.size() - 6, 6, ".local") == 0) 
        {
            normalized.erase(normalized.size() - 6);
        }
        return normalized;
    }
}

MdnsMgr& MdnsMgr::instance()
{
    static MdnsMgr inst;
    return inst;
}

MdnsMgr::MdnsMgr()
{
    respondersMutex_ = xSemaphoreCreateMutex();
    if (respondersMutex_ == nullptr) {
        ESP_LOGE(TAG, "Unable to create mDNS responder mutex");
        return;
    }


}

void MdnsMgr::init()
{
    // Initialization code for the mDNS manager can be added here if needed.
    if (xTaskCreate(responderTaskEntry, "mdnsResponderTask", 4096, this, 4, &responderTaskHandle_) != pdPASS) 
    {
        ESP_LOGE(TAG, "Unable to create mDNS responder task");
        responderTaskHandle_ = nullptr;
    }
}
void MdnsMgr::responderTaskEntry(void* param)
{
    static_cast<MdnsMgr*>(param)->responderTask();
}

void MdnsMgr::responderTask()
{
    while (true) 
    {
        std::unordered_map<std::string, MdnsHostInfo> responders;
        if (queryResponders(responders) && respondersMutex_ != nullptr) 
        {
            if (xSemaphoreTake(respondersMutex_, pdMS_TO_TICKS(1000)) == pdTRUE) 
            {
                responders_ = std::move(responders);
                xSemaphoreGive(respondersMutex_);
            }
        }
        if (DebugControl::enabled(DebugItem::MDNS)) 
        {
            printMdnsResults();
        }
        vTaskDelay(MDNS_RESPONDER_PERIOD);
    }
}

bool MdnsMgr::queryResponders(std::unordered_map<std::string, MdnsHostInfo>& responders)
{
    mdns_result_t* results = nullptr;
    esp_err_t err = mdns_query_ptr("_http", "_tcp", MDNS_RESPONDER_QUERY_TIMEOUT,
                                   MDNS_RESPONDER_MAX_RESULTS, &results);
    if (err != ESP_OK) {
        if (DebugControl::enabled(DebugItem::MDNS)) 
        {
            ESP_LOGW(TAG, "mDNS responder query failed: %s", esp_err_to_name(err));
        }
        return false;
    }

    if (results == nullptr) {
        responders.clear();
        return true;
    }

    for (mdns_result_t* result = results; result != nullptr; result = result->next) 
    {
        if (result->hostname == nullptr) 
        {
            continue;
        }

        MdnsHostInfo hostInfo;
        hostInfo.name = normalizeHostname(result->hostname);
        //ESP_LOGI(TAG, "Found mDNS responder: result->hostname=%s", result->hostname);
        hostInfo.port = result->port;

        for (mdns_ip_addr_t* address = result->addr; address != nullptr; address = address->next) 
        {
            if (address->addr.type == ESP_IPADDR_TYPE_V4) {
                hostInfo.addressType = "IPv4";
            } else {
                hostInfo.addressType = "IPv6";
            }
            if (address->addr.type == ESP_IPADDR_TYPE_V4) 
            {
                char ipAddress[16] = {};
                inet_ntoa_r(address->addr.u_addr.ip4, ipAddress, sizeof(ipAddress));
                hostInfo.ipAddress = ipAddress;
                break;
            }
        }

        if (hostInfo.ipAddress.empty()) 
        {
            esp_ip4_addr_t address;
            if (mdns_query_a(hostInfo.name.c_str(), MDNS_RESPONDER_QUERY_TIMEOUT, &address) == ESP_OK) 
            {
                char ipAddress[16] = {};
                esp_ip4addr_ntoa(&address, ipAddress, sizeof(ipAddress));
                hostInfo.ipAddress = ipAddress;
                hostInfo.addressType = "IPv4";
            }
        }

        responders[hostInfo.name] = std::move(hostInfo);
    }

    mdns_query_results_free(results);
    return true;
}

bool MdnsMgr::getHostInfo(const std::string& hostname, MdnsHostInfo& hostInfo) const
{
    if (respondersMutex_ == nullptr || xSemaphoreTake(respondersMutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    const auto it = responders_.find(normalizeHostname(hostname));
    if (it != responders_.end()) {
        hostInfo = it->second;
        xSemaphoreGive(respondersMutex_);
        return true;
    }

    xSemaphoreGive(respondersMutex_);
    return false;
}

std::vector<MdnsHostInfo> MdnsMgr::getHostInfos() const
{
    std::vector<MdnsHostInfo> hostInfos;
    if (respondersMutex_ == nullptr || xSemaphoreTake(respondersMutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return hostInfos;
    }

    hostInfos.reserve(responders_.size());
    for (const auto& entry : responders_) {
        hostInfos.push_back(entry.second);
    }
    xSemaphoreGive(respondersMutex_);
    return hostInfos;
}

void MdnsMgr::printMdnsResults()
{
    if (respondersMutex_ == nullptr || xSemaphoreTake(respondersMutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    if (responders_.empty()) 
    {
        ESP_LOGI(TAG, "No mDNS responders found");
    }
    for (const auto& entry : responders_) 
    {
        const MdnsHostInfo& hostInfo = entry.second;
        ESP_LOGI(TAG, "mDNS responder: name=%s ip=%s address_type=%s port=%u",
             hostInfo.name.c_str(), hostInfo.ipAddress.c_str(),
             hostInfo.addressType.c_str(), hostInfo.port);
    }

    xSemaphoreGive(respondersMutex_);
}

#if 0
std::string MdnsMgr::resolveUpdateServerUrl(const std::string &updateServerUrl)
{
    if (DebugControl::enabled(DebugItem::MDNS)) 
    {
        ESP_LOGI(TAG, "Starting to resolve update server URL: %s", updateServerUrl.c_str());
    }
    const std::size_t schemeEnd = updateServerUrl.find("://");
    if (schemeEnd == std::string::npos) {
        return updateServerUrl;
    }

    const std::size_t hostStart = schemeEnd + 3;
    const std::size_t hostEnd = updateServerUrl.find_first_of("/?#", hostStart);
    const std::size_t authorityEnd = hostEnd == std::string::npos ? updateServerUrl.size() : hostEnd;
    std::string hostname = updateServerUrl.substr(hostStart, authorityEnd - hostStart);

    const std::size_t portSeparator = hostname.rfind(':');
    if (portSeparator != std::string::npos) {
        hostname.erase(portSeparator);
    }
    if (!hostname.empty() && hostname.back() == '.') {
        hostname.pop_back();
    }

    if (hostname != "r33-ota.local") {
        return updateServerUrl;
    }

    char addressBuffer[16] = {};
    char resolvedAddressBuffer[16] = {};
    bool foundAddress = false;
    bool urlArrivedWithoutARecord = false;
    constexpr uint32_t queryTimeout = 3000;

    for (int attempt = 1; attempt <= 2 && !foundAddress; ++attempt) 
    {
        mdns_result_t* results = nullptr;
        if (DebugControl::enabled(DebugItem::MDNS)) 
        {
            ESP_LOGI(TAG, "mDNS query attempt %d of 2 for r33-ota.local", attempt);
        }
        esp_err_t err = mdns_query_ptr("_http", "_tcp", queryTimeout, 20, &results);
        if (err != ESP_OK) 
        {
            ESP_LOGW(TAG, "mDNS query attempt %d failed: %s", attempt, esp_err_to_name(err));
            continue;
        }

        if (results == nullptr) 
        {
            if (DebugControl::enabled(DebugItem::MDNS)) 
            {
                ESP_LOGW(TAG, "mDNS query attempt %d returned no HTTP services", attempt);
            }
            continue;
        }

        for (mdns_result_t* result = results; result != nullptr; result = result->next) 
        {
            const char* resultHostname = result->hostname != nullptr ? result->hostname : "<none>";
            const char* addressType = "none";
            const char* address = "<none>";
            if (result->addr != nullptr) 
            {
                addressType = result->addr->addr.type == ESP_IPADDR_TYPE_V4 ? "IPv4" : "IPv6";
                if (result->addr->addr.type == ESP_IPADDR_TYPE_V4) 
                {
                    inet_ntoa_r(result->addr->addr.u_addr.ip4, addressBuffer, sizeof(addressBuffer));
                    address = addressBuffer;
                }
            }
            if (DebugControl::enabled(DebugItem::MDNS)) 
            {
                ESP_LOGI(TAG, "mDNS result: hostname=%s port=%u address_type=%s address=%s",
                        resultHostname, result->port, addressType, address);
            }
            if (result->hostname != nullptr && strcmp(result->hostname, "r33-ota") == 0) 
            {
                if (result->addr != nullptr && result->addr->addr.type == ESP_IPADDR_TYPE_V4) 
                {
                    foundAddress = true;
                    // save the ip address of the resolved r33-ota.local host
                    std::strncpy(resolvedAddressBuffer, addressBuffer, sizeof(resolvedAddressBuffer));
                    resolvedAddressBuffer[sizeof(resolvedAddressBuffer) - 1] = '\0';  // ensure null‑termination
                }
                else
                {
                    urlArrivedWithoutARecord = true;
                }
            }
        }
        mdns_query_results_free(results);

        if (!foundAddress && attempt == 1) 
        {
            if (DebugControl::enabled(DebugItem::MDNS)) 
            {
                ESP_LOGW(TAG, "r33-ota.local not found on first mDNS query; retrying");
            }
        }
    }
    if (urlArrivedWithoutARecord) 
    {
        ESP_LOGW(TAG, "r33-ota PTR/SRV answer arrived without an A record");
        // PTR/SRV answer arrived without the A record piggybacked on it;
        // resolve the address explicitly via a dedicated A-record query.
        esp_ip4_addr_t hostAddr;
        esp_err_t aErr = mdns_query_a("r33-ota", queryTimeout, &hostAddr);
        if (aErr == ESP_OK) 
        {
            esp_ip4addr_ntoa(&hostAddr, resolvedAddressBuffer, sizeof(resolvedAddressBuffer));
            ESP_LOGI(TAG, "Resolved r33-ota A record directly: %s", resolvedAddressBuffer);
            foundAddress = true;
        }
        else
        {
            ESP_LOGW(TAG, "mdns_query_a for r33-ota failed: %s", esp_err_to_name(aErr));
        }
    }
    if (!foundAddress) 
    {
        if (DebugControl::enabled(DebugItem::MDNS)) 
        {
            ESP_LOGE(TAG, "mDNS did not return an IPv4 address for r33-ota.local after two queries");
        }
        return {};
    }

    const std::size_t hostnamePortSeparator = updateServerUrl.find(':', hostStart);
    const std::size_t replacementEnd = hostnamePortSeparator != std::string::npos &&
                                                hostnamePortSeparator < authorityEnd
                                            ? hostnamePortSeparator
                                            : authorityEnd;

    std::string resolvedUrl = updateServerUrl;
    resolvedUrl.replace(hostStart, replacementEnd - hostStart, resolvedAddressBuffer);
    if (DebugControl::enabled(DebugItem::MDNS)) 
    {
        ESP_LOGI(TAG, "Resolved OTA server %s to %s", updateServerUrl.c_str(), resolvedUrl.c_str());
    }
    return resolvedUrl;    

}
#endif

std::string MdnsMgr::resolveMdnsUrl(const std::string &mdnsUrl)
{
    // if the url is not properly formatted or we cannot find the hostname, return an empty string
    if (DebugControl::enabled(DebugItem::MDNS)) 
    {
        ESP_LOGI(TAG, "Starting to resolve update server URL: %s", mdnsUrl.c_str());
    }
    // check to make sure the mdnsUrl has the proper format for a url
    // http://r33-controller.local:8000/search?q=esp32 
    const std::size_t schemeEnd = mdnsUrl.find("://");
    if (schemeEnd == std::string::npos) {
        return {};
    }

    const std::size_t hostStart = schemeEnd + 3;
    const std::size_t hostEnd = mdnsUrl.find_first_of("/?#", hostStart);
    const std::size_t authorityEnd = hostEnd == std::string::npos ? mdnsUrl.size() : hostEnd;
    std::string hostname = mdnsUrl.substr(hostStart, authorityEnd - hostStart);

    const std::size_t portSeparator = hostname.rfind(':');
    if (portSeparator != std::string::npos) 
    {
        // remove the port if there was one specified
        hostname.erase(portSeparator);
    }
    if (!hostname.empty() && hostname.back() == '.') 
    {
        // remove the trailing dot if present
        hostname.pop_back();
    }

    if (hostname.empty()) 
    {
        return {};
    }

    MdnsHostInfo hostInfo;
    if(getHostInfo(hostname, hostInfo))
    {
        const std::size_t hostnamePortSeparator = mdnsUrl.find(':', hostStart);
        const std::size_t replacementEnd = hostnamePortSeparator != std::string::npos &&
                                                    hostnamePortSeparator < authorityEnd
                                                ? hostnamePortSeparator
                                                : authorityEnd;

        std::string resolvedUrl = mdnsUrl;
        resolvedUrl.replace(hostStart, replacementEnd - hostStart, hostInfo.ipAddress);
        if (DebugControl::enabled(DebugItem::MDNS)) 
        {
            ESP_LOGI(TAG, "Resolved OTA server %s to %s", mdnsUrl.c_str(), resolvedUrl.c_str());
        }
        return resolvedUrl;   
    }
    else
    {
        return {};

    }
}