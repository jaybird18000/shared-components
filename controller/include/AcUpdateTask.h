#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"


// Global queue handle (created in AcUpdateTask_Start)
extern QueueHandle_t g_acUpdateQueue;

// Call this once during system startup
void AcUpdateTask_Start();