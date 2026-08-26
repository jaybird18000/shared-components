#pragma once

#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mdns.h"

#include "r33Client.h"   // your WebSocket client wrapper

class R33SunShadeController {
public:
    static R33SunShadeController& instance();

    void begin();

private:
    R33SunShadeController() = default;
    R33SunShadeController(const R33SunShadeController&) = delete;
    R33SunShadeController& operator=(const R33SunShadeController&) = delete;

    static void messageTaskEntry(void* param);
    static void measurementTaskEntry(void* param);

    void messageTaskLoop();
    void measurementTaskLoop();

    void handleWebClientMsg(int sockfd, const std::string &message);

private:
    static constexpr const char* TAG = "R33SunShadeController";

    // reconnect timing
    static constexpr uint32_t RECONNECT_DELAY_MS = 7000;

    // event bits
    static constexpr EventBits_t WS_EVENT_DISCONNECTED = (1 << 0);

    TaskHandle_t       messageTaskHandle        = nullptr;
    TaskHandle_t       measurementTaskHandle        = nullptr;

};