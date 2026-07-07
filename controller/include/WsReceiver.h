#ifndef WS_RECEIVER_H
#define WS_RECEIVER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_http_server.h>

class WsReceiver {
public:
    static void start(QueueHandle_t incomingQueue);

private:
    static void task(void* param);

};

#endif // WS_RECEIVER_H