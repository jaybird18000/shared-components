#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <string>
#include <functional>

class OtaManager {
public:
  using ProgressCallback = std::function<void(int progress)>;
  using StatusCallback = std::function<void(const std::string& status, bool success)>;

  OtaManager();
  ~OtaManager();

  /**
   * Initialize OTA manager with version information and callbacks
   */
  void init(const std::string& currentVersion);

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
  bool _isUpdating;

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
