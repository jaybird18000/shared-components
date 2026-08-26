#include "WsServer.h"
#include "ClientsList.h"
#include "wifiMgr.h"
#include "Pages.h"
#include "slavePages.h"
#include "sunShadePages.h"
#include "pwa_files.h"
//#include "Icon.h"
#include "IconData.h"
#include "DeviceConfig.h"
#include "TaskUtilities.h"
#include "DebugServices.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

static const char* TAG = "WsServer";

WsServer::WsServer()
    : server_(nullptr)
{

}

WsServer& WsServer::instance() {
    static WsServer inst;
    return inst;
}

httpd_handle_t WsServer::serverHandle() {
    return server_;
}

esp_err_t WsServer::start() {
    return startHttpd();
}

esp_err_t WsServer::startHttpd()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.task_priority    = 8;

    ESP_LOGI(TAG, "Starting HTTPD at priority %d", config.task_priority);
    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start FAILED: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "httpd_start SUCCESS");

    httpd_uri_t rootUri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = rootHandler,
        .user_ctx  = this,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    httpd_register_uri_handler(server_, &rootUri);

    httpd_uri_t apple_icon_uri = {
        .uri       = "/apple-touch-icon.png",
        .method    = HTTP_GET,
        .handler   = handle_apple_icon,
        .user_ctx  = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };

    httpd_register_uri_handler(server_, &apple_icon_uri);

    httpd_uri_t favicon_uri = {
        .uri       = "/favicon.png",
        .method    = HTTP_GET,
        .handler   = handle_apple_icon,
        .user_ctx  = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };

    httpd_register_uri_handler(server_, &favicon_uri);

    httpd_uri_t wsUri = {
        .uri       = "/ws",
        .method    = HTTP_GET,
        .handler   = wsHandler,
        .user_ctx  = this,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    httpd_register_uri_handler(server_, &wsUri);

    httpd_uri_t slaveUri = {
        .uri       = "/slave",
        .method    = HTTP_GET,
        .handler   = slaveWsHandler,
        .user_ctx  = this,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    httpd_register_uri_handler(server_, &slaveUri);

    ESP_LOGI(TAG, "URI handlers registered");
    debugServices::postDebug("Web server started");
    return ESP_OK;
}

esp_err_t WsServer::rootHandler(httpd_req_t* req)
{

    ESP_LOGI(TAG, "rootHandler called, len=%d", (int)strlen(kAppPageHtml));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    if(DeviceConfig::instance().isMasterDevice())
    {
        httpd_resp_send(req, kAppPageHtml, HTTPD_RESP_USE_STRLEN);
    }
    else if (DeviceConfig::instance().isSunShadeDevice())
    {
        httpd_resp_send(req, kSunShadePageHtml, HTTPD_RESP_USE_STRLEN);

    }
    else
    {
        httpd_resp_send(req, kSlavePageHtml, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

esp_err_t WsServer::manifestHandler(httpd_req_t* req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, kManifestJson, strlen(kManifestJson));
    return ESP_OK;
}

esp_err_t WsServer::serviceWorkerHandler(httpd_req_t* req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, kServiceWorkerJs, strlen(kServiceWorkerJs));
    return ESP_OK;
}

// esp_err_t WsServer::iconHandler(httpd_req_t* req)
// {
//     httpd_resp_set_type(req, "image/svg+xml");
//     httpd_resp_send(req, kAppIconSvg, strlen(kAppIconSvg));
//     return ESP_OK;
// }
esp_err_t WsServer::handle_apple_icon(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)APPLE_ICON_PNG, APPLE_ICON_PNG_LEN);
}

esp_err_t WsServer::wsHandler(httpd_req_t* req) {
    return WsServer::instance().handleWsCommon(req, false);
}

esp_err_t WsServer::slaveWsHandler(httpd_req_t* req) {
    return WsServer::instance().handleWsCommon(req, true);
}

esp_err_t WsServer::handleWsCommon(httpd_req_t* req, bool isSlave) {
//    ESP_LOGI("WS", "WebSocket handshake: GET %s", req->uri);
    bool justAddedClient = false;
    int sockfd = httpd_req_to_sockfd(req);
    const char* role = isSlave ? "SLAVE" : "BROWSER";
    ClientInfo::ClientType theType = ClientInfo::ClientType::BROWSER;
    if(isSlave)
    {
        theType = ClientInfo::ClientType::SLAVE;
    }

    if (ClientsList::instance().findClient(sockfd) == nullptr) {
        ESP_LOGI(TAG, "Adding %s client %d", role, sockfd);
        if(isSlave)
        {
            debugServices::postDebug("Adding slave client ");
        }
        else{
            debugServices::postDebug("Adding browser client ");
        }
        ClientInfo info(sockfd, isSlave ? "slave" : "ui", theType);
        ClientsList::instance().addClient(sockfd, info);
        justAddedClient = true;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    // read the header first to get the payload length
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ws recv header failed fd=%d err=%d closing socket", sockfd, ret);
        httpd_sess_trigger_close(WsServer::instance().serverHandle(), sockfd);
        ClientsList::instance().removeClient(sockfd);
        return ret;
    }

    if (frame.type == HTTPD_WS_TYPE_PING || frame.type == HTTPD_WS_TYPE_PONG)
    {
        // wsServer is setup to ping both browswer and slave clients.
        // wsServer only sees PONGS, it does not expose the PING reception to user code
        //ESP_LOGI(TAG, "WebSocket %s rcvd from socket %d", frame.type == HTTPD_WS_TYPE_PING ? "PING" : "PONG", sockfd);
        TaskUtilities::MsgItem item{};
        item.sockfd = sockfd;
        item.fromClient = isSlave;
        item.fromMaster = false;
        item.fromBrowser = !isSlave;
        item.frameType = frame.type;
        item.len = 0;
        item.fromTask = TaskUtilities::TaskNames::No_TASK_NAME;
        item.toTask = TaskUtilities::TaskNames::WS_SERVER_MGR_TASK;
        TaskUtilities::sendToQueue(TaskUtilities::TaskNames::WS_SERVER_MGR_TASK, &item, 0);

        return ESP_OK;
    }
    else if((frame.type == HTTPD_WS_TYPE_TEXT) || (frame.type == HTTPD_WS_TYPE_BINARY)) 
    {
        if(strcmp(role, "BROWSER") == 0 )
        {
            TaskUtilities::MsgItem item{};
            item.sockfd = sockfd;
            item.fromClient = isSlave;
            item.fromMaster = false;
            item.fromBrowser = !isSlave;
            item.frameType = frame.type;
            item.len = 0;
            if (frame.len > sizeof(item.data)) {
                ESP_LOGW(TAG, "payload too large (%u), truncating to %u",
                        (unsigned)frame.len, (unsigned)sizeof(item.data));
                frame.len = sizeof(item.data);
            }

            item.len = frame.len;
            frame.payload = reinterpret_cast<uint8_t*>(item.data);
            // read the payload into item.data
            ret = httpd_ws_recv_frame(req, &frame, frame.len);
            item.data[frame.len] = '\0'; // null-terminate the string
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "ws recv payload failed fd=%d err=%d", sockfd, ret);
                return ret;
            }
            #if 0
            if(DeviceConfig::instance().isSlaveDevice())
            {
                if(item.fromBrowser)
                {
                    ESP_LOGI(TAG, "Slave device received message from %s fd=%d msg %s", role, item.sockfd, item.data);
                }

            }
            #endif

            item.fromTask = TaskUtilities::TaskNames::No_TASK_NAME;
            item.toTask = TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK;
            TaskUtilities::sendToQueue(TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK, &item, 0);
        }
        else
        {
            // deserialize() expects the full serialized MsgItem (header fields + data),
            // so the buffer must be able to hold more than just item.data
            static constexpr size_t kMaxSlavePayload = sizeof(TaskUtilities::MsgItem);
            uint8_t payloadBuf[kMaxSlavePayload];

            if (frame.len > sizeof(payloadBuf)) {
                ESP_LOGW(TAG, "payload too large (%u), truncating to %u",
                        (unsigned)frame.len, (unsigned)sizeof(payloadBuf));
                frame.len = sizeof(payloadBuf);
            }

            frame.payload = payloadBuf;
            // read the payload into payloadBuf
            ret = httpd_ws_recv_frame(req, &frame, frame.len);

            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "ws recv payload failed fd=%d err=%d", sockfd, ret);
                return ret;
            }
            TaskUtilities::MsgItem item{};
            item.deserialize(frame.payload, frame.len);
            // must replace the sockfd in the deserialized item with the actual socket fd
            item.sockfd = sockfd;
            //item.print();
            item.data[item.len] = '\0'; // null-terminate the string
            if(DeviceConfig::instance().isMasterDevice())
            {
                if(item.msgType == TaskUtilities::MsgTypes::DEBUG)
                {
                    // send all debug messages from clients to the browser controller
                    item.toTask = TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK;
                    TaskUtilities::sendToQueue(TaskUtilities::TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK, &item, 0);
                }
                else
                {
                    item.toTask = TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK;
                    TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_MASTER_CONTROLLER_TASK, &item, 0);
                }
            }
            else
            {
                ESP_LOGW(TAG, "rcvd slave uri, but device is not master from %s fd=%d msg %s", role, item.sockfd, item.data);
            }
        }
    }
    else
    {
        ESP_LOGW(TAG, "WebSocket unknown frame type %d from socket %d", frame.type, sockfd);
        return ESP_OK;
    }


    return ESP_OK;
}

//
// SAFE ASYNC SEND CONTEXT
//

struct WsSendContext {
    int sockfd;
    httpd_ws_frame_t frame;
    uint8_t* payload;
};

void WsServer::sendWork(void* arg)
{
    WsSendContext* ctx = static_cast<WsSendContext*>(arg);
    if (!ctx) return;

    httpd_ws_send_frame_async(instance().server_, ctx->sockfd, &ctx->frame);

    if (ctx->payload)
        free(ctx->payload);

    free(ctx);
}

//
// SAFE SEND TEXT
//

void WsServer::sendTextMsg(int sockfd, const std::string& msg)
{
    WsSendContext* ctx = (WsSendContext*)malloc(sizeof(WsSendContext));
    if (!ctx) return;
    memset(ctx, 0, sizeof(WsSendContext));

    ctx->sockfd = sockfd;

    if (!msg.empty()) {
        ctx->payload = (uint8_t*)malloc(msg.size());
        if (!ctx->payload) {
            free(ctx);
            return;
        }
        memcpy(ctx->payload, msg.data(), msg.size());
        ctx->frame.payload = ctx->payload;
        ctx->frame.len     = msg.size();
    }

    ctx->frame.type = HTTPD_WS_TYPE_TEXT;

    httpd_queue_work(server_, &WsServer::sendWork, ctx);
}

void WsServer::sendBinaryMsg(int sockfd, const uint8_t* data, size_t len)
{
    // Allocate context
    WsSendContext* ctx = (WsSendContext*)malloc(sizeof(WsSendContext));
    if (!ctx) return;
    memset(ctx, 0, sizeof(WsSendContext));

    ctx->sockfd = sockfd;

    // Allocate payload buffer
    if (data && len > 0) {
        ctx->payload = (uint8_t*)malloc(len);
        if (!ctx->payload) {
            free(ctx);
            return;
        }

        memcpy(ctx->payload, data, len);
        ctx->frame.payload = ctx->payload;
        ctx->frame.len     = len;
    }

    // Set WebSocket frame type to binary
    ctx->frame.type = HTTPD_WS_TYPE_BINARY;

    // Queue async send
    httpd_queue_work(server_, &WsServer::sendWork, ctx);
}

//
// SAFE SEND PING
//

void WsServer::sendPingMsg(int sockfd)
{
    WsSendContext* ctx = (WsSendContext*)malloc(sizeof(WsSendContext));
    if (!ctx) return;
    memset(ctx, 0, sizeof(WsSendContext));

    ctx->sockfd       = sockfd;
    ctx->payload      = nullptr;
    ctx->frame.type   = HTTPD_WS_TYPE_PING;
    ctx->frame.payload = nullptr;
    ctx->frame.len    = 0;

    httpd_queue_work(server_, &WsServer::sendWork, ctx);
}

//
// BROADCAST
//
// this function sends a text message to all connected clients of the specified audience type (slaves, browsers, or both).
void WsServer::broadcastText(const std::string& msg, AudienceType audience)
{
    ClientsList::instance().forEachClient([&](int fd, ClientInfo& info) {
        if ((info.type == ClientInfo::ClientType::SLAVE) &&
            (audience == AudienceType::SLAVES || audience == AudienceType::BOTH)) {
            sendTextMsg(fd, msg);
        } else if ((info.type == ClientInfo::ClientType::BROWSER) &&
                   (audience == AudienceType::BROWSERS || audience == AudienceType::BOTH)) {
            sendTextMsg(fd, msg);
        }
    });
}

void WsServer::broadcastPing()
{
    ClientsList::instance().forEachClient([&](int fd, ClientInfo& info) {
        sendPingMsg(fd);
    });
}