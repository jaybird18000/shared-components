#include "SharedDataStore.h"
#include "esp_timer.h"
#include "String"
#include "cstring"

SharedAcData_t SharedDataStore::data = {0.0f, 0.0f, 0.0f, 0.0f, "", 0ULL};
SemaphoreHandle_t SharedDataStore::mutex = nullptr;

void SharedDataStore::init() {
    mutex = xSemaphoreCreateMutex();
}

void SharedDataStore::set(float voltage, float current, float freq, float watts, const char* genStatus)
 {
    if (xSemaphoreTake(mutex, portMAX_DELAY)) {
        data.voltage = voltage;
        data.current = current;
        data.frequency = freq;
        data.watts = watts;
        // ⭐ SAFE COPY INTO FIXED BUFFER
        strncpy(data.genStatus, genStatus, sizeof(data.genStatus) - 1);
        data.genStatus[sizeof(data.genStatus) - 1] = '\0';   // ensure null termination
        data.timestampMs = esp_timer_get_time() / 1000ULL;
        xSemaphoreGive(mutex);
    }
}

SharedAcData_t SharedDataStore::get() {
    SharedAcData_t copy;
    if (xSemaphoreTake(mutex, portMAX_DELAY)) {
        copy = data;
        xSemaphoreGive(mutex);
    }
    return copy;
}