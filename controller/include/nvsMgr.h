#ifndef NVS_MGR_H
#define NVS_MGR_H

#include <string>
#include "wifiMgr.h"


class NvsMgr {
public:
    NvsMgr();
    static NvsMgr& instance();
    void initialize();

    void saveSTA_Config(const std::string& ssid, const std::string& password, bool isMaster = false);
    void saveAP_Config(const std::string &ssid, 
                                const std::string &password,
                                const std::string &ipAddress,
                                const std::string &gateway,
                                const std::string &netmask);
    void saveAll_Config(const std::string &sta_ssid, const std::string &sta_password, 
                        const std::string &ap_ssid, const std::string &ap_password, 
                        const std::string &ap_ipAddress, const std::string &ap_gateway, const std::string &ap_netmask);

    void loadSTA_config();
    void loadAP_config();
    void loadAll_config();
    void dumpNVS();
    WifiConfig currentSTA_Config() const;
    WifiConfig currentAP_Config() const;    
private:

    WifiConfig STA_config_;
    WifiConfig AP_config_;


};

#endif // NVS_MGR_H