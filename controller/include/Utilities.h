#pragma once
#include <string>
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/icmp.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
namespace Utilities
{
    //static void ping_results(ping_target_id_t id, esp_ping_found* pf);
    bool ping_addr(const char* ip);
    uint16_t checksum(void* data, int len);
    bool raw_icmp_ping(const char* ip);
    void sendDummyUdpPacket(const char* ip, int port);

    std::string floatToString(float value);
    int extractJsonInt(const std::string& payload, const char* field);
    bool extractJsonBool(const std::string& payload, const char* field);
    std::string extractJsonField(const std::string& payload, const char* field);
};