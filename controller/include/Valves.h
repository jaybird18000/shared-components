#ifndef VALVES_H
#define VALVES_H

#include <stdint.h>
#include <string>

enum class ValveState {
    Idle,
    Opening,
    Closing,
    Open,
    Closed,
    Error
};

class Valve {
public:
    Valve(int openPin, int closePin, int limitOpenPin, int limitClosedPin, const char* name);
    void init();
    void open();
    void close();
    void stop();
    void update();
    void setStateByReadingLimitSwitches();
    ValveState state() const;
    const char* statusText() const;
    static const char* statusText(ValveState theState);
    const char* name() const;

private:
    bool isOpenLimit() const;
    bool isClosedLimit() const;
    int openPin_;
    int closePin_;
    int limitOpenPin_;
    int limitClosedPin_;
    const char* name_;
    ValveState state_;
    int64_t actionStartUs_;
    static constexpr int64_t kActionTimeoutUs = 15LL * 1000000LL;
    bool testModeIdle_;
    bool testModeError_;
};

class ValvesController {
public:
    static ValvesController& instance();

    ValvesController();
    void init();
    void openGenerator();
    void closeGenerator();
    void openAirConditioner();
    void closeAirConditioner();
    void update();
    const Valve& generatorValve() const;
    const Valve& acValve() const;

private:
    Valve generatorValve_;
    Valve acValve_;

};

#endif // VALVES_H
