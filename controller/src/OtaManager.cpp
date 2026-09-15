#include "OtaManager.h"
#include "ClientsList.h"
#include "wsServer.h"
#include "DebugServices.h"
#include "nvsMgr.h"
#include "version.h"
#include "DeviceConfig.h"
#include "TaskUtilities.h"
#include "Utilities.h"
#include "wifiMgr.h"
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <mdns.h>
#include <lwip/inet.h>
//#include <app_update.h>
#include <ArduinoJson.h>
#include <cstring>
#include "esp_log.h"

static const char* TAG = "OTA_MANAGER";

TaskHandle_t OtaManager::otaTaskHandle = nullptr;
TaskHandle_t OtaManager::healthTaskHandle = nullptr;
OtaManager& OtaManager::instance()
{
    static OtaManager inst;
    return inst;
}
OtaManager::OtaManager() 
    : _isUpdating(false) 
{

}

OtaManager::~OtaManager() {
}

void OtaManager::init(const std::string& currentVersion) {
  _currentVersion = currentVersion;
  _availableVersion = currentVersion;
  ESP_LOGI(TAG, "OTA Manager initialized with version: %s", _currentVersion.c_str());
}

void OtaManager::begin()
{
    TaskUtilities::createTaskWithQueue(TaskUtilities::TaskNames::OTA_MANAGER_MESSAGE_TASK, messageTaskEntry, sizeof(TaskUtilities::MsgItem));
    if (healthTaskHandle == nullptr) {
        xTaskCreate(healthTaskEntry, "OtaHealthTask", 4096, nullptr, 4, &healthTaskHandle);
    }
}

void OtaManager::messageTaskEntry(void *param)
{
    OtaManager::messageTaskLoop();
}

void OtaManager::messageTaskLoop()
{
    ESP_LOGI(TAG, "Starting OTA Manager message task loop");
    while (true) 
    {
        TaskUtilities::MsgItem item;

        if (TaskUtilities::waitOnQueue(TaskUtilities::TaskNames::OTA_MANAGER_MESSAGE_TASK, &item, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "Processing msg from %s: %s",
                      TaskUtilities::taskNameToString(item.fromTask), std::string(item.data, item.len).c_str());
            if(item.fromBrowser)
            {
                ClientsList::instance().updateLastSeen(item.sockfd);
                const char* role = item.fromBrowser ? "BROWSER" : "SLAVE";
                //ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());
                OtaManager::handleBrowserClientMsg(item.sockfd, item);

            }
            else
            {
                const char* role = item.fromBrowser ? "BROWSER" : "SLAVE";
                //ESP_LOGI(TAG, "Msg rcvd fd=%d from %s msg %s", item.sockfd, role, payload.c_str());
                OtaManager::handleSlaveClientMsg(item.sockfd, item);
            }

        }
    }
}
void OtaManager::handleBrowserClientMsg(int sockfd, TaskUtilities::MsgItem& item)
{
    std::string message(item.data, item.len);  // local, safe

    if (message.find("get_ota_server_status") != std::string::npos) {
        if (healthTaskHandle != nullptr) {
            xTaskNotifyGive(healthTaskHandle);
        }
        return;
    }
    if (message.find("get_ota_url") != std::string::npos) {
       std::string savedHost = NvsMgr::instance().getUpdateServerHost();
        std::string savedPort = NvsMgr::instance().getUpdateServerPort();
        std::string json = "{\"type\":\"ota_url_loaded\",\"host\":\"" + savedHost + "\",\"port\":\"" + savedPort + "\"}";
        WsServer::instance().sendTextMsg(sockfd, json);
        return;
    }
    if (message.find("get_current_version") != std::string::npos) {
        std::string currentVersion = getFirmwareVersion();
        std::string json = "{\"type\":\"current_version_info\",\"version\":\"" + currentVersion + "\"}";
        WsServer::instance().sendTextMsg(sockfd, json);
        return;
    }
    if (message.find("check_updates") != std::string::npos) {
        std::string updateServerUrl = Utilities::extractJsonField(message, "update_server_url");
        if (!updateServerUrl.empty()) {
            debugServices::postDebug("Checking for firmware updates...");
            
            // Create OTA task with its own stack
            OtaTaskParams* params = new OtaTaskParams();
            params->updateServerUrl = updateServerUrl;
            params->operation = 0;  // check_updates
            
            xTaskCreate(otaTaskFunction, "OtaTask", 4096, params, 5, &otaTaskHandle);
        }
        return;
    }
    if (message.find("start_update") != std::string::npos) {
        std::string updateServerUrl = Utilities::extractJsonField(message, "update_server_url");
        if (!updateServerUrl.empty()) {
            debugServices::postDebug("Starting firmware update...");
            
            // Create OTA task with its own stack
            OtaTaskParams* params = new OtaTaskParams();
            params->updateServerUrl = updateServerUrl;
            params->operation = 1;  // start_update
            
            xTaskCreate(otaTaskFunction, "OtaUpdateTask", 4096, params, 5, &otaTaskHandle);
        }
        return;
    }  
}
void OtaManager::handleSlaveClientMsg(int sockfd, TaskUtilities::MsgItem& item)
{
    ESP_LOGW(TAG, "HandleSlaveClientCalled, but no processing done");
}

void OtaManager::healthTaskEntry(void* param)
{
    OtaManager::healthTaskLoop();
}

void OtaManager::healthTaskLoop()
{
    ESP_LOGI(TAG, "Starting OTA server health check task");
    while (true)
    {
        // Wakes on a get_ota_server_status notification or every period
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HEALTH_CHECK_PERIOD_MS));
        ESP_LOGI(TAG, "Checking OTA server health...");
        checkServerHealth();
        ESP_LOGI(TAG, "OTA server health check complete");
    }
}

void OtaManager::checkServerHealth()
{
    // The Python server handles one request at a time; do not interrupt a download
    if (OtaManager::instance()._isUpdating) {
        return;
    }

    bool online = false;
    std::string host = NvsMgr::instance().getUpdateServerHost();
    std::string port = NvsMgr::instance().getUpdateServerPort();

    if (!host.empty() && !port.empty() && WifiMgr::instance().isSTAConnected()) {
        std::string baseUrl = "http://" + host + ":" + port;
        std::string healthUrl = OtaManager::instance().resolveUpdateServerUrl(baseUrl);
        if (!healthUrl.empty()) {
          healthUrl += "/health?id=";
          switch (DeviceConfig::instance().getDeviceRole()) {
            case DeviceRole::MASTER:
              healthUrl += "r33-master-01";
              break;
            case DeviceRole::SLAVE:
              healthUrl += "r33-slave-01";
              break;
            case DeviceRole::SUNSHADE:
              healthUrl += "r33-sunshade-01";
              break;
          }
          healthUrl += "&version=" + getFirmwareVersion();
          healthUrl += "&health_timeout_ms=" +
                       std::to_string(HEALTH_CHECK_PERIOD_MS * 2 + 3000);

            esp_http_client_config_t config = {};
            config.url = healthUrl.c_str();
            config.method = HTTP_METHOD_GET;
            config.timeout_ms = 2500;

            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client) {
                if (esp_http_client_open(client, 0) == ESP_OK) {
                    esp_http_client_fetch_headers(client);
                    online = esp_http_client_get_status_code(client) == 200;
                }
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
            }
        }
    }

    std::string json = std::string("{\"type\":\"ota_server_status\",\"online\":") + (online ? "true" : "false") + "}";
    WsServer::instance().broadcastText(json, WsServer::AudienceType::BROWSERS);
}

// Task function for OTA operations (runs on separate stack)
void OtaManager::otaTaskFunction(void* pvParameters) {
    OtaTaskParams* params = (OtaTaskParams*)pvParameters;
    
    ESP_LOGI(TAG, "OTA task started: operation=%d, url=%s", params->operation, params->updateServerUrl.c_str());
    
    if (params->operation == 0) {
        // Check for updates
        std::string currentVersion = getFirmwareVersion();
        OtaManager::instance().setStatusCallback([currentVersion](const std::string& version, bool success) {
            if (success) {
                std::string json = "{\"type\":\"version_info\",\"current_version\":\"" + currentVersion + "\",\"available_version\":\"" + version + "\"}";
                WsServer::instance().broadcastText(json, WsServer::AudienceType::BROWSERS);
                debugServices::postDebug("Available version: " + version);
            } else {
                debugServices::postDebug("Update check failed");
            }
        });
        
        OtaManager::instance().init(currentVersion);
        OtaManager::instance().checkForUpdates(params->updateServerUrl);
    } 
    else if (params->operation == 1) {
        // Start update - track progress to avoid debug spam
        int lastPostedProgress = 0;
        OtaManager::instance().setProgressCallback([&lastPostedProgress](int progress) {
            std::string json = "{\"type\":\"update_progress\",\"progress\":" + std::to_string(progress) + "}";
            WsServer::instance().broadcastText(json, WsServer::AudienceType::BROWSERS);
            
            // Only post debug for every 20% increase or at 100%
            if (progress >= lastPostedProgress + 20 || progress == 100) {
                lastPostedProgress = progress;
                debugServices::postDebug("Update progress: " + std::to_string(progress) + "%");
            }
        });
        
        OtaManager::instance().setStatusCallback([](const std::string& message, bool success) {
            std::string json = "{\"type\":\"update_status\",\"status\":\"" + std::string(success ? "success" : "error") + "\",\"message\":\"" + message + "\"}";
            WsServer::instance().broadcastText(json, WsServer::AudienceType::BROWSERS);
            if (success) {
                debugServices::postDebug("✓ " + message);
            } else {
                debugServices::postDebug("✗ " + message);
            }
        });
        
        OtaManager::instance().init(getFirmwareVersion());
        OtaManager::instance().startUpdate(params->updateServerUrl);
    }
    
    delete params;
    vTaskDelete(NULL);
}

std::string OtaManager::getCurrentVersion() const {
  return _currentVersion;
}

std::string OtaManager::getAvailableVersion() const {
  return _availableVersion;
}

void OtaManager::setProgressCallback(ProgressCallback callback) {
  _progressCallback = callback;
}

void OtaManager::setStatusCallback(StatusCallback callback) {
  _statusCallback = callback;
}

void OtaManager::cancel() {
  _isUpdating = false;
}

void OtaManager::checkForUpdates(const std::string& updateServerUrl) {
  if (updateServerUrl.empty()) {
    if (_statusCallback) {
      _statusCallback("Invalid update server URL", false);
    }
    return;
  }

  // Save URL to NVS for later use
  ESP_LOGI(TAG, "Checking for updates from: %s", updateServerUrl.c_str());
  fetchVersionInfo(updateServerUrl);
}

#if 0
std::string OtaManager::resolveUpdateServerUrl(const std::string& updateServerUrl) {
  ESP_LOGI(TAG, "Resolving update server URL: %s", updateServerUrl.c_str());
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

  mdns_result_t* results = nullptr;
  uint32_t queryTimeout = 1000;
  esp_err_t err = mdns_query_ptr("_http", "_tcp", queryTimeout, 20, &results);
  if (err != ESP_OK || results == nullptr) {
    ESP_LOGE(TAG, "Failed to discover OTA server via mDNS: %s", esp_err_to_name(err));
    return {};
  }

  char addressBuffer[16] = {};
  bool foundAddress = false;
  for (mdns_result_t* result = results; result != nullptr; result = result->next) {
    const char* resultHostname = result->hostname != nullptr ? result->hostname : "<none>";
    const char* addressType = "none";
    const char* address = "<none>";
    if (result->addr != nullptr) {
      addressType = result->addr->addr.type == ESP_IPADDR_TYPE_V4 ? "IPv4" : "IPv6";
      if (result->addr->addr.type == ESP_IPADDR_TYPE_V4) {
        inet_ntoa_r(result->addr->addr.u_addr.ip4, addressBuffer, sizeof(addressBuffer));
        address = addressBuffer;
      }
    }
    ESP_LOGI(TAG, "mDNS result: hostname=%s port=%u address_type=%s address=%s",
             resultHostname, result->port, addressType, address);

    if (result->hostname == nullptr || strcmp(result->hostname, "r33-ota") != 0 ||
        result->addr == nullptr || result->addr->addr.type != ESP_IPADDR_TYPE_V4) {
      continue;
    }

    inet_ntoa_r(result->addr->addr.u_addr.ip4, addressBuffer, sizeof(addressBuffer));
    foundAddress = true;
    break;
  }
  mdns_query_results_free(results);

  if (!foundAddress) {
    ESP_LOGE(TAG, "mDNS did not return an IPv4 address for r33-ota.local");
    return {};
  }

  const std::size_t hostnamePortSeparator = updateServerUrl.find(':', hostStart);
  const std::size_t replacementEnd = hostnamePortSeparator != std::string::npos &&
                                             hostnamePortSeparator < authorityEnd
                                         ? hostnamePortSeparator
                                         : authorityEnd;

  std::string resolvedUrl = updateServerUrl;
  resolvedUrl.replace(hostStart, replacementEnd - hostStart, addressBuffer);
  ESP_LOGI(TAG, "Resolved OTA server %s to %s", updateServerUrl.c_str(), resolvedUrl.c_str());
  return resolvedUrl;
}
#endif

std::string OtaManager::resolveUpdateServerUrl(const std::string& updateServerUrl) {
  ESP_LOGI(TAG, "Resolving update server URL: %s", updateServerUrl.c_str());
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
  bool foundAddress = false;
  constexpr uint32_t queryTimeout = 1000;

  for (int attempt = 1; attempt <= 2 && !foundAddress; ++attempt) {
    mdns_result_t* results = nullptr;
    ESP_LOGI(TAG, "mDNS query attempt %d of 2 for r33-ota.local", attempt);
    esp_err_t err = mdns_query_ptr("_http", "_tcp", queryTimeout, 20, &results);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "mDNS query attempt %d failed: %s", attempt, esp_err_to_name(err));
      continue;
    }

    if (results == nullptr) {
      ESP_LOGW(TAG, "mDNS query attempt %d returned no HTTP services", attempt);
      continue;
    }

    for (mdns_result_t* result = results; result != nullptr; result = result->next) {
      const char* resultHostname = result->hostname != nullptr ? result->hostname : "<none>";
      const char* addressType = "none";
      const char* address = "<none>";
      if (result->addr != nullptr) {
        addressType = result->addr->addr.type == ESP_IPADDR_TYPE_V4 ? "IPv4" : "IPv6";
        if (result->addr->addr.type == ESP_IPADDR_TYPE_V4) {
          inet_ntoa_r(result->addr->addr.u_addr.ip4, addressBuffer, sizeof(addressBuffer));
          address = addressBuffer;
        }
      }
      ESP_LOGI(TAG, "mDNS result: hostname=%s port=%u address_type=%s address=%s",
               resultHostname, result->port, addressType, address);

      if (result->hostname != nullptr && strcmp(result->hostname, "r33-ota") == 0 &&
          result->addr != nullptr && result->addr->addr.type == ESP_IPADDR_TYPE_V4) {
        foundAddress = true;
        break;
      }
    }
    mdns_query_results_free(results);

    if (!foundAddress && attempt == 1) {
      ESP_LOGW(TAG, "r33-ota.local not found on first mDNS query; retrying");
    }
  }

  if (!foundAddress) {
    ESP_LOGE(TAG, "mDNS did not return an IPv4 address for r33-ota.local after two queries");
    return {};
  }

  const std::size_t hostnamePortSeparator = updateServerUrl.find(':', hostStart);
  const std::size_t replacementEnd = hostnamePortSeparator != std::string::npos &&
                                             hostnamePortSeparator < authorityEnd
                                         ? hostnamePortSeparator
                                         : authorityEnd;

  std::string resolvedUrl = updateServerUrl;
  resolvedUrl.replace(hostStart, replacementEnd - hostStart, addressBuffer);
  ESP_LOGI(TAG, "Resolved OTA server %s to %s", updateServerUrl.c_str(), resolvedUrl.c_str());
  return resolvedUrl;
}

void OtaManager::fetchVersionInfo(const std::string& updateServerUrl) {
  std::string resolvedUrl = resolveUpdateServerUrl(updateServerUrl);
  if (resolvedUrl.empty()) {
    if (_statusCallback) {
      _statusCallback("Could not resolve OTA server via mDNS", false);
    }
    return;
  }

  // Build metadata URL (append /metadata.json if not already there)
  std::string metadataUrl = resolvedUrl;
  if (metadataUrl.back() != '/') {
    metadataUrl += "/";
  }
  metadataUrl += "metadata.json";

  ESP_LOGI(TAG, "Fetching metadata from: %s", metadataUrl.c_str());

  esp_http_client_config_t config = {};
  config.url = metadataUrl.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 5000;
  config.buffer_size = 2048;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    if (_statusCallback) {
      _statusCallback("Failed to initialize HTTP client", false);
    }
    return;
  }

  // Open connection and send request
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    if (_statusCallback) {
      _statusCallback(std::string("HTTP request failed: ") + esp_err_to_name(err), false);
    }
    esp_http_client_cleanup(client);
    return;
  }

  // Fetch headers to get content length
  int content_length = esp_http_client_fetch_headers(client);
  
  int status_code = esp_http_client_get_status_code(client);
  if (status_code != 200) {
    ESP_LOGE(TAG, "HTTP status code: %d", status_code);
    if (_statusCallback) {
      _statusCallback(std::string("HTTP error: ") + std::to_string(status_code), false);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
  }

  if (content_length <= 0 || content_length > 2048) {
    ESP_LOGE(TAG, "Invalid content length: %d", content_length);
    if (_statusCallback) {
      _statusCallback("Invalid metadata size", false);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
  }

  char* response_data = (char*)malloc(content_length + 1);
  if (!response_data) {
    if (_statusCallback) {
      _statusCallback("Memory allocation failed", false);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
  }

  int bytes_read = esp_http_client_read(client, response_data, content_length);
  if (bytes_read <= 0) {
    ESP_LOGE(TAG, "Failed to read response body: %d bytes", bytes_read);
    if (_statusCallback) {
      _statusCallback("Failed to read response", false);
    }
    free(response_data);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
  }
  
  response_data[bytes_read] = '\0';

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  // Parse JSON metadata with minimal stack allocation
  DynamicJsonDocument doc(1024);  // Very small buffer
  DeserializationError error = deserializeJson(doc, response_data);
  // Do not free response data until after processing the doc below
  // there is some sort of race condition with memory allocation
  if (error) {
    ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
    if (_statusCallback) {
      _statusCallback(std::string("JSON parse error: ") + error.c_str(), false);
    }
    free(response_data);
    return;
  }

  // Extract version and firmware URL
  // Expected structure: { "latest_version": "1.0.2", "versions": { "r33-master": { "latest": "...", "url": "..." } } }

  // Extract available version
  std::string availableVersion = _currentVersion;
  if (doc["latest_version"].is<const char*>()) {
    availableVersion = doc["latest_version"].as<const char*>();
    ESP_LOGI(TAG, "Found latest_version in metadata: %s", availableVersion.c_str());
  } else if (doc["versions"]["r33-master"]["latest"].is<const char*>()) {
    availableVersion = doc["versions"]["r33-master"]["latest"].as<const char*>();
    ESP_LOGI(TAG, "Found version in versions.r33-master.latest: %s", availableVersion.c_str());
  }
  free(response_data);
  _availableVersion = availableVersion;

  ESP_LOGI(TAG, "Current version: %s, Available version: %s", 
           _currentVersion.c_str(), _availableVersion.c_str());

  if (_statusCallback) {
    _statusCallback(_availableVersion, true);
  }
}

void OtaManager::startUpdate(const std::string& updateServerUrl) {
  if (_isUpdating) {
    if (_statusCallback) {
      _statusCallback("Update already in progress", false);
    }
    return;
  }

  if (updateServerUrl.empty()) {
    if (_statusCallback) {
      _statusCallback("Invalid update server URL", false);
    }
    return;
  }

  std::string resolvedUrl = resolveUpdateServerUrl(updateServerUrl);
  if (resolvedUrl.empty()) {
    if (_statusCallback) {
      _statusCallback("Could not resolve OTA server via mDNS", false);
    }
    return;
  }

  _isUpdating = true;

  // Build firmware URL dynamically based on device role
  std::string firmwareUrl = resolvedUrl;
  if (firmwareUrl.back() != '/') {
    firmwareUrl += "/";
  }
  
  // Determine device name (master or slave) from device role
   switch (DeviceConfig::instance().getDeviceRole()) {
    case DeviceRole::MASTER:
        firmwareUrl += "master/r33-master.bin";
        break;
    case DeviceRole::SLAVE:
        firmwareUrl += "slave/r33-slave.bin";
        break;
    case DeviceRole::SUNSHADE:
        firmwareUrl += "sunshade/r33-sunshade.bin";
        break;
    default:
        if (_statusCallback) {
            _statusCallback("Unknown device role", false);
        }
        _isUpdating = false;
        return;
  }
  ESP_LOGI(TAG, "Starting firmware update from: %s", firmwareUrl.c_str());
  downloadAndInstallFirmware(firmwareUrl);
}

void OtaManager::downloadAndInstallFirmware(const std::string& firmwareUrl) {
  esp_http_client_config_t config = {};
  config.url = firmwareUrl.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 30000;
  config.buffer_size = 2048;  // Reduced from 8192

  esp_ota_handle_t update_handle = 0;
  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    ESP_LOGE(TAG, "Failed to find OTA update partition");
    if (_statusCallback) {
      _statusCallback("Failed to find OTA partition", false);
    }
    _isUpdating = false;
    return;
  }

  esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start OTA: %s", esp_err_to_name(err));
    if (_statusCallback) {
      _statusCallback(std::string("OTA begin failed: ") + esp_err_to_name(err), false);
    }
    _isUpdating = false;
    return;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    if (_statusCallback) {
      _statusCallback("HTTP client initialization failed", false);
    }
    esp_ota_abort(update_handle);
    _isUpdating = false;
    return;
  }

  err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    if (_statusCallback) {
      _statusCallback(std::string("HTTP connection failed: ") + esp_err_to_name(err), false);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    esp_ota_abort(update_handle);
    _isUpdating = false;
    return;
  }

  int total_length = esp_http_client_fetch_headers(client);
  int downloaded = 0;
  int bytes_read;
  char* buffer = (char*)malloc(2048);  // Reduced from 4096 to avoid stack issues
  
  if (!buffer) {
    ESP_LOGE(TAG, "Failed to allocate download buffer");
    if (_statusCallback) {
      _statusCallback("Memory allocation failed", false);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    esp_ota_abort(update_handle);
    _isUpdating = false;
    return;
  }

  while (_isUpdating) {
    bytes_read = esp_http_client_read(client, buffer, 2048);
    if (bytes_read == 0) break;

    err = esp_ota_write(update_handle, (const void*)buffer, bytes_read);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
      break;
    }

    downloaded += bytes_read;
    int progress = (total_length > 0) ? (downloaded * 100 / total_length) : 0;
    if (_progressCallback) {
      _progressCallback(progress);
    }
  }

  free(buffer);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  ESP_LOGI(TAG, "Downloaded %d bytes total", downloaded);

  if (!_isUpdating) {
    ESP_LOGI(TAG, "Update cancelled");
    esp_ota_abort(update_handle);
    if (_statusCallback) {
      _statusCallback("Update cancelled", false);
    }
    return;
  }

  err = esp_ota_end(update_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
    if (_statusCallback) {
      _statusCallback(std::string("OTA end failed: ") + esp_err_to_name(err), false);
    }
    _isUpdating = false;
    return;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
    if (_statusCallback) {
      _statusCallback(std::string("Boot partition set failed: ") + esp_err_to_name(err), false);
    }
    _isUpdating = false;
    return;
  }

  ESP_LOGI(TAG, "Firmware update completed successfully");
  if (_statusCallback) {
    _statusCallback("Firmware updated successfully. Rebooting...", true);
  }

  _isUpdating = false;
  // Reboot after a short delay
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
}


