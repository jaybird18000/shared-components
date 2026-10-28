
#include "mdns.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string>
#include <unordered_map>
#include <vector>

struct MdnsHostInfo {
    std::string name;
    std::string ipAddress;
    std::string addressType = "none";
    uint16_t port = 0;
};

class MdnsMgr {
    public:
        static MdnsMgr& instance();
        void init();
        std::string resolveMdnsUrl(const std::string& mdnsUrl);
        bool getHostInfo(const std::string& hostname, MdnsHostInfo& hostInfo) const;
        std::vector<MdnsHostInfo> getHostInfos() const;
        void printMdnsResults();

    private:
        MdnsMgr();
        MdnsMgr(const MdnsMgr&) = delete;
        MdnsMgr& operator=(const MdnsMgr&) = delete;

        static void responderTaskEntry(void *param);
        void responderTask();
        bool queryResponders(std::unordered_map<std::string, MdnsHostInfo>& responders);

        mutable SemaphoreHandle_t respondersMutex_ = nullptr;
        std::unordered_map<std::string, MdnsHostInfo> responders_;
        TaskHandle_t responderTaskHandle_ = nullptr;
};