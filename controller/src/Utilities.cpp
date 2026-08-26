#include "Utilities.h"
#include "esp_log.h"
//void ping_results(ping_target_id_t id, esp_ping_found *pf)
//{
//}

bool Utilities::ping_addr(const char *ip)
{
    return raw_icmp_ping(ip);
}

uint16_t Utilities::checksum(void *data, int len)
{
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)data;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(uint8_t*)ptr;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

bool Utilities::raw_icmp_ping(const char *ip)
{
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        ESP_LOGE("PING", "Failed to create raw socket");
        return false;
    }

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &target.sin_addr);

    // Build ICMP Echo Request
    uint8_t packet[64];
    memset(packet, 0, sizeof(packet));

    struct icmp_echo_hdr* hdr = (struct icmp_echo_hdr*)packet;
    hdr->type = ICMP_ECHO;
    hdr->code = 0;
    hdr->id   = lwip_htons(0x1234);
    hdr->seqno = lwip_htons(1);

    hdr->chksum = 0;
    hdr->chksum = checksum(packet, sizeof(packet));

    // Send ICMP Echo Request
    int sent = sendto(sock, packet, sizeof(packet), 0,
                      (struct sockaddr*)&target, sizeof(target));

    if (sent < 0) {
        ESP_LOGE("PING", "sendto failed");
        close(sock);
        return false;
    }

    // Wait for ICMP Echo Reply (optional)
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t reply[128];
    int len = recv(sock, reply, sizeof(reply), 0);

    close(sock);

    if (len > 0) {
        ESP_LOGI("PING", "Ping reply received from %s", ip);
        return true;
    } else {
        ESP_LOGW("PING", "Ping timeout from %s", ip);
        return false;
    }
}

// port 9 is the discard port, often used for testing
void Utilities::sendDummyUdpPacket(const char *ip, int port = 9)
{
    // send dummy udp packet
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in addr = {
        .sin_len    = sizeof(struct sockaddr_in),
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr   = {0},        // will be overwritten by inet_pton
        .sin_zero   = {0},
    };
    inet_pton(AF_INET, ip, &addr.sin_addr);

    sendto(sock, "x", 1, 0, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
}

std::string Utilities::extractJsonField(const std::string& payload, const char* field)
{
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = payload.find(needle);
    if (pos == std::string::npos) return {};
    size_t colon = payload.find(':', pos);
    if (colon == std::string::npos) return {};
    size_t start = payload.find('"', colon + 1);
    if (start == std::string::npos) return {};
    size_t end = payload.find('"', start + 1);
    if (end == std::string::npos) return {};
    return payload.substr(start + 1, end - start - 1);
}
bool Utilities::extractJsonBool(const std::string& payload, const char* field)
{
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = payload.find(needle);
    if (pos == std::string::npos) return false;
    size_t colon = payload.find(':', pos);
    if (colon == std::string::npos) return false;
    size_t start = payload.find_first_not_of(" \t", colon + 1);
    if (start == std::string::npos) return false;
    std::string value = payload.substr(start, 4);
    if (value == "true") return true;
    if (value == "false") return false;
    return false;
}
int Utilities::extractJsonInt(const std::string& payload, const char* field)
{
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = payload.find(needle);
    if (pos == std::string::npos) return -1;

    size_t colon = payload.find(':', pos);
    if (colon == std::string::npos) return -1;

    // Find start of number (skip spaces)
    size_t start = payload.find_first_of("0123456789", colon + 1);
    if (start == std::string::npos) return -1;

    // Find end of number
    size_t end = payload.find_first_not_of("0123456789", start);

    return std::stoi(payload.substr(start, end - start));
}
std::string Utilities::floatToString(float value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    return std::string(buf);
}