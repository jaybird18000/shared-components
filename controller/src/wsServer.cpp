#include "WsServer.h"
#include "ClientsList.h"
#include "wifiMgr.h"
#include "Pages.h"
#include "slavePages.h"
#include "pwa_files.h"
//#include "Icon.h"
#include "IconData.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

static const char* TAG = "WsServer";

WsServer::WsServer()
    : server_(nullptr)
{
    incomingQueue_ = xQueueCreate(20, sizeof(IncomingMsg));
    debugMsgCounter_ = 1;
}

WsServer& WsServer::instance() {
    static WsServer inst;
    return inst;
}

QueueHandle_t WsServer::incomingQueue() {
    return incomingQueue_;
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
    postDebug("Web server started");
    return ESP_OK;
}

esp_err_t WsServer::rootHandler(httpd_req_t* req)
{

    ESP_LOGI(TAG, "rootHandler called, len=%d", (int)strlen(kAppPageHtml));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    if(WifiMgr::isMaster())
    {
        httpd_resp_send(req, kAppPageHtml, HTTPD_RESP_USE_STRLEN);
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
    int sockfd = httpd_req_to_sockfd(req);
    const char* role = isSlave ? "SLAVE" : "BROWSER";
    ClientInfo::ClientType theType = ClientInfo::ClientType::BROWSER;
    if(isSlave)
    {
        theType = ClientInfo::ClientType::SLAVE;
    }
    ClientInfo info(sockfd, isSlave ? "slave" : "ui", theType);
    if (ClientsList::instance().findClient(sockfd) == nullptr) {
        ESP_LOGI(TAG, "Adding %s client %d", role, sockfd);
        if(isSlave)
        {
            postDebug("Adding slave client ");
        }
        else{
            postDebug("Adding browser client ");
        }
        ClientsList::instance().addClient(sockfd, info);
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ws recv header failed fd=%d err=%d closing socket", sockfd, ret);
        httpd_sess_trigger_close(WsServer::instance().serverHandle(), sockfd);
        ClientsList::instance().removeClient(sockfd);
        return ret;
    }

    IncomingMsg msg{};
    msg.sockfd  = sockfd;
    msg.type    = frame.type;
    msg.isSlave = isSlave;

    if (frame.type == HTTPD_WS_TYPE_PING || frame.type == HTTPD_WS_TYPE_PONG) {
        msg.len = 0;
        xQueueSend(instance().incomingQueue_, &msg, 0);
        return ESP_OK;
    }

    if (frame.len > sizeof(msg.data)) {
        ESP_LOGW(TAG, "payload too large (%u), truncating to %u",
                 (unsigned)frame.len, (unsigned)sizeof(msg.data));
        frame.len = sizeof(msg.data);
    }

    msg.len      = frame.len;
    frame.payload = reinterpret_cast<uint8_t*>(msg.data);

    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ws recv payload failed fd=%d err=%d", sockfd, ret);
        return ret;
    }

    xQueueSend(instance().incomingQueue_, &msg, 0);
    return ESP_OK;
}

void WsServer::postDebug(const std::string& message)
{
    // Assign ID
    uint32_t id = debugMsgCounter_++;

    // Determine if message already starts with "S:" or "M:"
    bool hasPrefix =
        (message.rfind("S:", 0) == 0) ||
        (message.rfind("M:", 0) == 0);

    // Build final message string
    std::string msg;
    if (hasPrefix) {
        msg = message;   // leave as-is
    } else {
        msg = WifiMgr::isMaster()
                ? "M: " + message
                : "S: " + message;
    }

    // Build the full JSON string ONCE
    std::string json =
        std::string("{\"type\":\"debug\",\"id\":") +
        std::to_string(id) +
        ",\"message\":\"" + msg + "\"}";

    //ESP_LOGW(TAG, "Pushing debug message debugLog_ %s, id=%u", json.c_str(), id);

    // Store the FULL JSON string in the log
    debugLog_.push_back({id, json});
    while (debugLog_.size() > 500) {
        debugLog_.pop_front();
    }

    // Broadcast the SAME JSON string
    broadcastDebugText(id, json, AudienceType::BROWSERS);
}
void WsServer::postDebug(const char* fmt, ...)
{
    char buffer[256];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Reuse your existing overload
    postDebug(std::string(buffer));
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
// this function sends the debug message to the web clients (browser)
// It will also forward the debug messaage to the master if we are the slave 
void WsServer::
broadcastDebugText(uint32_t id, const std::string& msg, AudienceType audience)
{
    ClientsList::instance().forEachClient([&](int fd, ClientInfo& info) 
    {

        // send to master if we are the slave
        if (info.type == ClientInfo::ClientType::MASTER)
        {
//            ESP_LOGW(TAG, "Slave forwarding debug message to master fd=%d, id=%u %s", fd, id, msg.c_str());

            // send message to the slave controller command queue so it can forward to the master
            if (slaveControllerCommandQueue) {
                if((id - info.lastSentMasterDebugMsgCtr) > 1)
                {
                    ESP_LOGW(TAG, "Slave syncing debug msgs with master");
                    sendDebugMsgsSinceToR33SlaveControllerCommandQueue(info.lastSentMasterDebugMsgCtr);

                }
                else
                {
                    sendMsgToR33SlaveControllerCommandQueue(msg);
                }
                info.lastSentMasterDebugMsgCtr = id;
            } 
            else 
            {
                ESP_LOGW(TAG, "No slave r33Controller commandQueue set — cannot forward message");
            }   
        } 
        else if ((info.type == ClientInfo::ClientType::BROWSER) &&
                (audience == AudienceType::BROWSERS || audience == AudienceType::BOTH)) 
        {
            std::string me = WifiMgr::isMaster() ? "MASTER - " : "SLAVE - ";

            if(info.debugMessagesInSync)
            {
//                ESP_LOGW(TAG, "%s sending msg to browser fd=%d, id=%u", me.c_str(), fd, id);

                sendTextMsg(fd, msg);
                // update the last sent debug message counter for this client
                info.lastSentDebugMsgCtr = id;
            }
            else
            {
                ESP_LOGW(TAG, "%s Browser Client fd=%d not in sync, skipping debug message id=%u", me.c_str(), fd, id);
            }

        }

    });
}

void WsServer::sendDebugMsgsSince(int msgCtr, int sockfd)
{
    // send all debug messages with id > msgCtr to the specified sockfd
    for (const auto& entry : debugLog_) {
        if (entry.id > msgCtr) {
            sendTextMsg(sockfd, entry.message);

            ESP_LOGW(TAG, "Syncing: Sending debug message to browser client fd=%d, id=%u", sockfd, entry.id);
            vTaskDelay(pdMS_TO_TICKS(5));   // 5–10 ms is enough
        }
    }
    ClientsList::instance().findClient(sockfd)->debugMessagesInSync = true;
}

void WsServer::sendDebugMsgsSinceToR33SlaveControllerCommandQueue(int msgCtr)
{
    // send all debug messages with id > msgCtr to the slave controller command queue
    for (const auto& entry : debugLog_) {
        if (entry.id > msgCtr) {
            sendMsgToR33SlaveControllerCommandQueue(entry.message);

            ESP_LOGW(TAG, "Slave Syncing Master: Sending debug message to slave controller command queue id=%u", entry.id);
            vTaskDelay(pdMS_TO_TICKS(5));   // 5–10 ms is enough
        }
    }
}
void WsServer::sendMsgToR33SlaveControllerCommandQueue(const std::string& msg)
{
    if (slaveControllerCommandQueue) {
        // Copy message into heap so queue owns it
        char* copy = (char*)malloc(msg.size() + 1);
        if (copy) {
            memcpy(copy, msg.c_str(), msg.size() + 1);

            if (xQueueSend(slaveControllerCommandQueue, &copy, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Slave ControllerCommand queue full, dropping message");
                free(copy);
            }
        }
    } 
    else 
    {
        ESP_LOGW(TAG, "No slave controllercommand queue set — cannot forward message");
    }   
}
void WsServer::broadcastPing()
{
    ClientsList::instance().forEachClient([&](int fd, ClientInfo& info) {
        sendPingMsg(fd);
    });
}