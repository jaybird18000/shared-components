#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "TaskUtilities.h"
#include <atomic>
#include <esp_websocket_client.h>
#include <string>
#include <functional>

class OtaManager {
public:
  static OtaManager& instance();
  using ProgressCallback = std::function<void(int progress)>;
  using StatusCallback = std::function<void(const std::string& status, bool success)>;

  OtaManager();
  ~OtaManager();

  /**
   * Initialize OTA manager with version information and callbacks
   */
  void init(const std::string& currentVersion);

  void begin();
  bool isFirmwareServerOnline() const;

  /**
   * Check for available updates from the specified server URL
   * Calls statusCallback with available version or error message
   */
  void checkForUpdates(const std::string& updateServerUrl);

  /**
   * Start firmware update from the specified server URL
   * Calls progressCallback for each progress update (0-100)
   * Calls statusCallback when complete (success or error)
   */
  void startUpdate(const std::string& updateServerUrl);

  static void messageTaskEntry(void *param);

  static void messageTaskLoop();

  static void handleBrowserClientMsg(int sockfd, TaskUtilities::MsgItem &item);
  static void handleSlaveClientMsg(int sockfd, TaskUtilities::MsgItem &item);

  struct OtaTaskParams {
      std::string updateServerUrl;
      int operation;  // 0 = check_updates, 1 = start_update
  };

  static void otaTaskFunction(void *pvParameters);

  static void healthTaskEntry(void *param);
  static void healthTaskLoop();
  static void checkServerHealth();
  static void commandTaskEntry(void *param);
  static void commandTaskLoop();
  static void commandWebSocketEvent(void* event_handler_arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);

  /**
   * Get the current firmware version
   */
  std::string getCurrentVersion() const;

  /**
   * Get the latest available version (after checkForUpdates)
   */
  std::string getAvailableVersion() const;

  /**
   * Set progress callback function
   */
  void setProgressCallback(ProgressCallback callback);

  /**
   * Set status callback function
   */
  void setStatusCallback(StatusCallback callback);

  /**
   * Stop any ongoing update operation
   */
  void cancel();

private:
  std::string _currentVersion;
  std::string _availableVersion;
  ProgressCallback _progressCallback;
  StatusCallback _statusCallback;
    // OTA task handle
  static TaskHandle_t otaTaskHandle;
  static TaskHandle_t healthTaskHandle;
  static TaskHandle_t commandTaskHandle;
  std::string resolvedServerUrl;
  bool _isUpdating;
  std::atomic<bool> firmwareServerOnline_{false};
  esp_websocket_client_handle_t commandClient_ = nullptr;
  std::string commandClientIp_;
  std::string lastCommandId_;
  static constexpr uint32_t HEALTH_CHECK_PERIOD_MS = 8000;
  static constexpr uint32_t COMMAND_RECONNECT_PERIOD_MS = 2000;

  static std::string deviceId();
  void sendCommandStatus(const std::string& commandId, const std::string& status);

  /**
   * Fetch metadata from update server to get available version
   */
  void fetchVersionInfo(const std::string& updateServerUrl);

  /**
   * Download and install firmware from URL using esp_https_ota
   */
  void downloadAndInstallFirmware(const std::string& firmwareUrl);
};
#endif // OTA_MANAGER_H
