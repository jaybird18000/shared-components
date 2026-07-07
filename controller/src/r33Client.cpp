#include "r33Client.h"
#include "ClientsList.h"
#include "WsServer.h"
#include <esp_http_server.h>

R33Client::R33Client() {}

R33Client::~R33Client() {
    stop();
}

const char* R33Client::opcodeToStr(int op) {
    switch (op) {
        case 0: return "CONTINUATION";
        case 1: return "TEXT";
        case 2: return "BINARY";
        case 8: return "CLOSE";
        case 9: return "PING";
        case 10: return "PONG";
        default: return "UNKNOWN";
    }
}

bool R33Client::begin(const char* masterHostname, int port, const char* path) {

    // 🔒 PROTECTION #1 — prevent double‑begin()
    if (client != nullptr) {
        ESP_LOGW(TAG, "begin() called but client already exists (%p). Ignoring.", client);
        return false;
    }

    wsUri = "ws://" + std::string(masterHostname) + ":" + std::to_string(port) + path;
    ESP_LOGI(TAG, "Connecting to master WebSocket: %s", wsUri.c_str());

    esp_websocket_client_config_t cfg = {};
    cfg.uri = wsUri.c_str();

    // we will be handleliing the reconnect logic
    cfg.disable_auto_reconnect = true;
    cfg.reconnect_timeout_ms = 5000;
    cfg.network_timeout_ms = 5000;   // 5 seconds

#if 1
    // WebSocket heartbeat
    cfg.ping_interval_sec = 5;
    cfg.pingpong_timeout_sec = 3;
    cfg.disable_pingpong_discon = false;

    // TCP keepalive (backup)
    cfg.keep_alive_enable = true;
    cfg.keep_alive_idle = 5;
    cfg.keep_alive_interval = 5;
    cfg.keep_alive_count = 3;
#endif
    // cfg.ping_interval_sec = 0;
    // cfg.pingpong_timeout_sec = 0;
    // cfg.disable_pingpong_discon = true;
    client = esp_websocket_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init websocket client");
        return false;
    }

    // 🔒 PROTECTION #3 — ensure handler is registered only once
    esp_websocket_register_events(client,
                                  WEBSOCKET_EVENT_ANY,
                                  &R33Client::eventHandler,
                                  this);

    esp_err_t err = esp_websocket_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start websocket client: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(client);
        client = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Started websocket client successfully");
    clientStarted = true;
    return true;
}

void R33Client::stop() {

    // 🔒 PROTECTION #4 — only stop if valid
    if (client == nullptr)
        return;

    ESP_LOGW(TAG, "Stopping WebSocket client %p", client);

    // Stop and destroy safely
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);

    client = nullptr;
    connected = false;
    clientStarted = false;
}
void R33Client::setClientDisconnectedCallback(std::function<void(int sockfd)> cb)
{
    clientDisconnectedCallback_ = cb;
}
void R33Client::notifyClientDisconnected(int sockfd)
{
    if (clientDisconnectedCallback_) {
        clientDisconnectedCallback_(sockfd);
    }
}
bool R33Client::send(const std::string& msg) {
    if (!client) {
        ESP_LOGW(TAG, "send() called with null client");
        return false;
    }
    if (!connected) {
        ESP_LOGW(TAG, "send() called while not connected");
        return false;
    }

    // finite timeout instead of portMAX_DELAY
    const TickType_t timeoutTicks = pdMS_TO_TICKS(1000);

    int sent = esp_websocket_client_send_text(
        client,
        msg.c_str(),
        msg.length(),
        timeoutTicks
    );

    if (sent <= 0) {
        ESP_LOGW(TAG, "send() failed, sent=%d", sent);
        connected = false;
        return false;
    }

    return true;
}

void R33Client::eventHandler(void* handler_args,
                             esp_event_base_t base,
                             int32_t event_id,
                             void* event_data)
{
    R33Client* self = static_cast<R33Client*>(handler_args);
    self->handleEvent(base, event_id, event_data);
}

void R33Client::handleEvent(esp_event_base_t base,
                            int32_t event_id,
                            void* event_data)
{
    switch (event_id) {

        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to master");
            connected = true;
            if(ClientsList::instance().findClient(1) == nullptr)
            {
                ESP_LOGI(TAG,"Adding %s client %d","MASTER", 1);

                // we just connected to the master, the master is not a slave 
                // and the sockfd is 1 since no other connections
                ClientInfo info(1, "Master", ClientInfo::ClientType::MASTER);
                ClientsList::instance().addClient(1, info);
                ClientsList::instance().updateLastSeen(1);
                ClientsList::instance().updatePingSent(1, false);
//                WsServer::instance().sendDebugMsgsSinceToR33SlaveControllerCommandQueue(0);
                WsServer::instance().postDebug("Adding master client ");
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from master");
            ClientsList::instance().removeClient(1);
            WsServer::instance().postDebug("Removing master client ");
            connected = false;
            // ⭐ MANUAL RECONNECT TRIGGER
            reconnectRequested = true;
            reconnectTimestamp = esp_log_timestamp();
            break;

        case WEBSOCKET_EVENT_DATA: 
        {
            auto* data = (esp_websocket_event_data_t*)event_data;
            // ⭐ Detect PING

            if (data->op_code == HTTPD_WS_TYPE_PING) {
//                ESP_LOGW(TAG, "Received PING from master");
                // No JSON, no queue — just update lastSeen
                ClientsList::instance().updateLastSeen(1);
                ClientsList::instance().updatePingSent(1, false);
                break;
            }

            // ⭐ Detect PONG
            if (data->op_code == HTTPD_WS_TYPE_PONG) {
//                ESP_LOGI(TAG, "Received PONG from master");
                ClientsList::instance().updateLastSeen(1);
                ClientsList::instance().updatePingSent(1, false);
                break;
            }

            // ⭐ TEXT or BINARY frames (JSON commands)
            if (data->op_code == HTTPD_WS_TYPE_TEXT || data->op_code == HTTPD_WS_TYPE_BINARY)
            {            
                std::string msg(data->data_ptr, data->data_len);
                // ESP_LOGI(TAG, "Received from master: type: %s msg: %s",
                //           opcodeToStr(data->op_code), msg.c_str());
                ClientsList::instance().updateLastSeen(1);
                ClientsList::instance().updatePingSent(1, false);
                // ⭐ Forward message to slave controller queue
                if (commandQueue) {
                    // Copy message into heap so queue owns it
                    char* copy = (char*)malloc(msg.size() + 1);
                    if (copy) {
                        memcpy(copy, msg.c_str(), msg.size() + 1);

                        if (xQueueSend(commandQueue, &copy, 0) != pdTRUE) {
                            ESP_LOGW(TAG, "Command queue full, dropping message");
                            free(copy);
                        }
                    }
                } 
                else 
                {
                    ESP_LOGW(TAG, "No command queue set — cannot forward message");
                }            
                break;
            }
            else{
                ESP_LOGW(TAG, "Unknown frame type rcvd %d", data->op_code);

            }
            break;
        }

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            // ⭐ MANUAL RECONNECT TRIGGER
            connected = false;
            reconnectRequested = true;
            reconnectTimestamp = esp_log_timestamp();
            break;
    }

    if(reconnectRequested)
    {
        R33Client::notifyClientDisconnected(1);
        reconnectRequested = false;
    }
}