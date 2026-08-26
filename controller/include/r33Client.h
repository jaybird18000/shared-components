#pragma once

#include <string>
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <atomic>
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

    bool sendBinary(const uint8_t *data, size_t len);

    bool testMethod();
    
    // Check if connected
    bool isConnected() const { return connected.load(); }

    bool isStarted() const { return clientStarted.load();}

    void setClientDisconnectedCallback(std::function<void(int sockfd)> cb);
    void notifyClientDisconnected(int sockfd);

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

    SemaphoreHandle_t clientMutex = nullptr;
    int tempVar1 = 1;
    int tempVar2 = 2;
    esp_websocket_client_handle_t client = nullptr;
    std::atomic_bool connected = false;
    std::atomic_bool clientStarted = false;
    std::string wsUri;
    std::function<void(int sockFd)> clientDisconnectedCallback_;
    static constexpr const char* TAG = "r33Client";
};