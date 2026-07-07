#pragma once

#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mdns.h"

#include "r33Client.h"   // your WebSocket client wrapper

class R33SlaveController {
public:
    static R33SlaveController& instance();

    void begin();

private:
    R33SlaveController() = default;
    R33SlaveController(const R33SlaveController&) = delete;
    R33SlaveController& operator=(const R33SlaveController&) = delete;

    static void socketTaskEntry(void* param);
    static void messageTaskEntry(void* param);
    static void measurementTaskEntry(void* param);

    void socketTaskLoop();
    void messageTaskLoop();
    void measurementTaskLoop();

    void processJsonCommand(const char* json);
    void startGenerator();
    void stopGenerator();
    bool discoverMaster(std::string& hostname, std::string& ip, int& port);
    bool isPortOpen(const std::string& ip, int port, int timeoutMs);
    std::string collectACData();

    void onClientDisconnected(int sockfd);

private:
    static constexpr const char* TAG = "R33SlaveController";

    // reconnect timing
    static constexpr uint32_t RECONNECT_DELAY_MS = 7000;

    // event bits
    static constexpr EventBits_t WS_EVENT_DISCONNECTED = (1 << 0);

    TaskHandle_t       socketTaskHandle        = nullptr;
    TaskHandle_t       messageTaskHandle        = nullptr;
    TaskHandle_t       measurementTaskHandle        = nullptr;

    QueueHandle_t      commandQueue = nullptr;
    EventGroupHandle_t wsEvents          = nullptr;

    std::string masterHost;
    std::string masterIp;
    int         masterPort               = 80;
    uint32_t    lastReconnectAttemptMs   = 0;

    R33Client   client;
};