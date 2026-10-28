#pragma once
#include "valves.h"
#include "genRelay.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class StatusDb {
public:
    static StatusDb& instance();

    void setTestModeBasedOnDeviceRole(bool enabled);

    // Setters
    void setMasterTestMode(bool enabled);
    void setSlaveTestMode(bool enabled);
    void setSunShadeTestMode(bool enabled);

    void setGeneratorValveState(ValveState state);
    void setACValveState(ValveState state);

    void setGeneratorState(GenRelay::GenStates status);

    void setGeneratorVoltage(float voltage);
    void setGeneratorCurrent(float current);
    void setGeneratorPower(float power);
    void setGeneratorFrequency(float frequency);

    void setAcData(float voltage, float current, float frequency, float watts, GenRelay::GenStates genStatus);

    // Getters
    bool getMasterTestMode();
    bool getSlaveTestMode();
    bool getSunShadeTestMode();

    ValveState getGeneratorValveState();
    ValveState getACValveState();

    GenRelay::GenStates getGeneratorState();

    float getGeneratorVoltage();
    float getGeneratorCurrent();
    float getGeneratorPower();
    float getGeneratorFrequency();

private:
    StatusDb();  // private constructor for singleton

    // Internal state
    bool masterTestMode_;
    bool slaveTestMode_;
    bool sunShadeTestMode_;

    float generatorVoltage_;
    float generatorCurrent_;
    float generatorPower_;
    float generatorFrequency_;

    ValveState generatorValveState_;
    GenRelay::GenStates generatorStatus_;
    ValveState acValveState_;

    // Mutex for thread safety
    SemaphoreHandle_t mutex_;
};