#pragma once

#include <string>
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include <functional>

class R33Client {
public:
    R33Client();
    ~R33Client();

    // Configure and start the client
    bool begin(const char* masterHostname, int port = 80, const char* path = "/ws");

    // Stop and cleanup
    void stop();

    // Send a message to the master
    bool send(const std::string& msg);

    // Check if connected
    bool isConnected() const { return connected; }

    bool isStarted() const { return clientStarted;}

    void setClientDisconnectedCallback(std::function<void(int sockfd)> cb);
    void notifyClientDisconnected(int sockfd);


    void setCommandQueue(QueueHandle_t q) { commandQueue = q; }

private:
    static void eventHandler(void* handler_args,
                             esp_event_base_t base,
                             int32_t event_id,
                             void* event_data);

    void handleEvent(esp_event_base_t base,
                     int32_t event_id,
                     void* event_data);

    const char* opcodeToStr(int op);
    bool reconnectRequested = false;
    uint32_t reconnectTimestamp = 0;
    
private:
    QueueHandle_t commandQueue = nullptr;
    esp_websocket_client_handle_t client = nullptr;
    bool connected = false;
    bool clientStarted = false;
    std::string wsUri;
    std::function<void(int sockFd)> clientDisconnectedCallback_;
    static constexpr const char* TAG = "r33Client";
};