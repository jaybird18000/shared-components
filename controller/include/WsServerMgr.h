#pragma once

#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class wsServerMgr {
public:
    static wsServerMgr& instance();
    static void start();

private:
    static void wsServerMgrTask(void * param);
    static void wsServerMgrPingTask(void* param);

};