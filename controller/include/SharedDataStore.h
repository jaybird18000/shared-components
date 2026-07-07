#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

typedef struct {
    float voltage;
    float current;
    float frequency;
    float watts;
    char genStatus[32];
    uint64_t timestampMs;
} SharedAcData_t;

class SharedDataStore {
public:
    static void init();
    static void set(float voltage, float current, float freq, float watts, const char* genStatus);
    static SharedAcData_t get();

private:
    static SharedAcData_t data;
    static SemaphoreHandle_t mutex;
};