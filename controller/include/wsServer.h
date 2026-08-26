#ifndef WS_SERVER_H
#define WS_SERVER_H

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>
#include <deque>

static inline const char* wsFrameTypeToString(httpd_ws_type_t type) {
    switch (type) {
        case HTTPD_WS_TYPE_CONTINUE: return "CONTINUE";
        case HTTPD_WS_TYPE_TEXT:     return "TEXT";
        case HTTPD_WS_TYPE_BINARY:   return "BINARY";
        case HTTPD_WS_TYPE_CLOSE:    return "CLOSE";
        case HTTPD_WS_TYPE_PING:     return "PING";
        case HTTPD_WS_TYPE_PONG:     return "PONG";
        default:                     return "UNKNOWN";
    }
};
enum AudienceType
{
    NONE,
    SLAVES,
    BROWSERS,
    BOTH
};
struct IncomingMsg {
    int sockfd;
    bool isSlave;
    httpd_ws_type_t type;
    uint16_t len;          // payload length
    char data[256];        // fixed buffer (tune size as needed)
};
class WsServer {
public:
    enum class AudienceType
    {
        NONE,
        SLAVES,
        BROWSERS,
        BOTH
    };

    struct DebugEntry {
        uint32_t id;
        std::string message;
    };
    static WsServer& instance();

    esp_err_t start();
    httpd_handle_t serverHandle();

    // Sending APIs

    void sendTextMsg(int sockfd, const std::string& msg);
    void sendBinaryMsg(int sockfd, const uint8_t* data, size_t len);
    void sendPingMsg(int sockfd);
    void broadcastText(const std::string& msg, AudienceType audience);

    void broadcastPing();

private:
    WsServer();
    esp_err_t startHttpd();

    static esp_err_t rootHandler(httpd_req_t* req);
    static esp_err_t wsHandler(httpd_req_t* req);
    static esp_err_t slaveWsHandler(httpd_req_t* req);
    static esp_err_t manifestHandler(httpd_req_t* req);
    static esp_err_t serviceWorkerHandler(httpd_req_t* req);
//    static esp_err_t iconHandler(httpd_req_t* req);
    static esp_err_t handle_apple_icon(httpd_req_t *req);

    esp_err_t handleWsCommon(httpd_req_t* req, bool isSlave);
    static void sendWork(void* arg);
    httpd_handle_t server_;

};

#endif // WS_SERVER_H
