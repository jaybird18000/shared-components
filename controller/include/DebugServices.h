#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>
#include <deque>
#include <cstdint>


namespace debugServices {

    enum class AudienceType
    {
        NONE,
        SLAVES,
        BROWSERS,
        BOTH
    };

    struct DebugEntry {
        uint32_t id;
        std::string message;
    };

    void init();
    void postDebug(const std::string& message);
    void postDebug(const char* fmt, ...);
    void broadcastDebugText(uint32_t id, const std::string& msg, AudienceType audience);
    void sendDebugMsgsSince(int msgCtr, int sockfd);
    int  sendDebugMsgsSinceToR33ClientControllerMessageQueue(int msgCtr);
    bool sendMsgToR33ClientControllerMessageQueue(const std::string& msg);
    void debugPrintAll(const char* tag = "DebugServices", bool toLog = true, bool toConsole = false);
    void debugPrintCounters(const char* tag = "DebugServices", bool toLog = true, bool toConsole = false);

    extern std::deque<DebugEntry> debugLog_;
    extern uint32_t debugMsgCounter_;
}