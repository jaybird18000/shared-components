#pragma once

#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mdns.h"

#include "r33Client.h"   // your WebSocket client wrapper

class R33ClientController {
public:
    static R33ClientController& instance();

    void startMessageTask();
    void startSocketTask();

    bool clientSend(const std::string &msg);
    bool clientSendBinary(const uint8_t *data, size_t len);
    bool isClientConnected();

private:
    R33ClientController() = default;
    R33ClientController(const R33ClientController&) = delete;
    R33ClientController& operator=(const R33ClientController&) = delete;

    static void socketTaskEntry(void* param);
    static void messageTaskEntry(void* param);

    void socketTaskLoop();
    void messageTaskLoop();

    bool processJsonDebugMessage(const char* json);

    bool discoverMaster(std::string& hostname, std::string& ip, int& port);
    bool isPortOpen(const std::string& ip, int port, int timeoutMs);
//    std::string collectACData();

    void onClientDisconnected(int sockfd);

private:
    static constexpr const char* TAG = "R33ClientController";

    // reconnect timing
    static constexpr uint32_t RECONNECT_DELAY_MS = 7000;

    // event bits
    static constexpr EventBits_t WS_EVENT_DISCONNECTED = (1 << 0);

    TaskHandle_t       socketTaskHandle        = nullptr;
    TaskHandle_t       messageTaskHandle        = nullptr;


    EventGroupHandle_t wsEvents          = nullptr;

    std::string masterHost;
    std::string masterIp;
    int         masterPort               = 80;
    uint32_t    lastReconnectAttemptMs   = 0;

    R33Client   r33Client;
};