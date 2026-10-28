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

    static void messageTaskEntry(void* param);
    static void measurementTaskEntry(void* param);

    void messageTaskLoop();
    void measurementTaskLoop();

    void handleWebClientMsg(int sockfd, const std::string &message);

    void startGenerator();
    void stopGenerator();

    std::string collectACData();

private:
    static constexpr const char* TAG = "R33SlaveController";

    TaskHandle_t       messageTaskHandle        = nullptr;
    TaskHandle_t       measurementTaskHandle        = nullptr;
    int testModeAcMonitorStartStopDelayCounter = 0; // Counter for delaying AC data generation in test mode
    bool generateGoodACData = false; // Flag to indicate if good AC data should be generated in test mode   

};