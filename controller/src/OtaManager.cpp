#include "OtaManager.h"
#include "nvsMgr.h"
#include <esp_ota_ops.h>
#include <esp_http_client.h>
//#include <app_update.h>
#include <ArduinoJson.h>
#include <cstring>
#include "esp_log.h"

static const char* TAG = "OTA_MANAGER";

OtaManager::OtaManager() 
    : _isUpdating(false) {
}

OtaManager::~OtaManager() {
}

void OtaManager::init(const std::string& currentVersion) {
  _currentVersion = currentVersion;
  _availableVersion = currentVersion;
  ESP_LOGI(TAG, "OTA Manager initialized with version: %s", _currentVersion.c_str());
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
  NvsMgr::instance().saveUpdateServerUrl(updateServerUrl);

  ESP_LOGI(TAG, "Checking for updates from: %s", updateServerUrl.c_str());
  fetchVersionInfo(updateServerUrl);
}

void OtaManager::fetchVersionInfo(const std::string& updateServerUrl) {
  // Build metadata URL (append /metadata.json if not already there)
  std::string metadataUrl = updateServerUrl;
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
  DynamicJsonDocument doc(512);  // Very small buffer
  DeserializationError error = deserializeJson(doc, response_data);
  free(response_data);

  if (error) {
    ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
    if (_statusCallback) {
      _statusCallback(std::string("JSON parse error: ") + error.c_str(), false);
    }
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

  _isUpdating = true;

  // Build firmware URL dynamically based on device role
  std::string firmwareUrl = updateServerUrl;
  if (firmwareUrl.back() != '/') {
    firmwareUrl += "/";
  }
  
  // Determine device name (master or slave) from device role
  bool isMaster = WifiMgr::instance().isMaster();
  if (isMaster) {
    firmwareUrl += "master/r33-master.bin";
  } else {
    firmwareUrl += "slave/r33-slave.bin";
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
