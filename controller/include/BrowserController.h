#pragma once
#include "TaskUtilities.h"

class BrowserController {
public:
    static BrowserController& instance();

    void begin();

private:
    BrowserController() = default;
    BrowserController(const BrowserController&) = delete;
    BrowserController& operator=(const BrowserController&) = delete;

    static void messageTaskEntry(void* param);
  
    static void messageTaskLoop();

    static void broadcastStatusTask(void *param);

    static void handleWebClientMsg(int sockfd, TaskUtilities::MsgItem &item);

    static void handleSlaveClientMsg(int sockfd, TaskUtilities::MsgItem &item);

    static void broadcastStatus();

    static void processJsonCommand(const char* json);


private:
    static constexpr const char* TAG = "BrowserController";


};