#pragma once

#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class r33MasterController {
public:
    static void start();

private:
    static void r33MasterControllerTask(void* param);

    static void handleWebClientMsg(int sockfd, const std::string &message);
    static void handleSlaveClientMsg(int sockfd, const std::string &message);

};