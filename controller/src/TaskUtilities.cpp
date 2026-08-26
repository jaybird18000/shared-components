#include <cstdint>
#include <cstring>
#include "TaskUtilities.h"
#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <array>

namespace TaskUtilities {

std::array<QueueHandle_t, static_cast<std::size_t>(TaskNames::Count)> s_queueHandles = {};
std::array<TaskHandle_t, static_cast<std::size_t>(TaskNames::Count)> s_taskHandles = {};

const TaskDefinition& definition(TaskNames taskName) {
    return TaskInfoByName[toIndex(taskName)];
}

BaseType_t createTask(
    TaskNames taskName,
    TaskFunction_t taskFunction,
    void* taskParam,
    TaskHandle_t* outTaskHandle
) {
    const std::size_t index = toIndex(taskName);
    TaskHandle_t createdTaskHandle = nullptr;
    const TaskDefinition& taskDefinition = definition(taskName);

    const BaseType_t result = xTaskCreate(
        taskFunction,
        taskDefinition.taskLabel,
        taskDefinition.stackDepth,
        taskParam,
        taskDefinition.priority,
        &createdTaskHandle
    );

    if (result == pdPASS) {
        s_taskHandles[index] = createdTaskHandle;
        if (outTaskHandle != nullptr) {
            *outTaskHandle = createdTaskHandle;
        }
    }

    return result;
}

BaseType_t createTaskWithQueue(
    TaskNames taskName,
    TaskFunction_t taskFunction,
    std::size_t queueItemSize,
    void* taskParam,
    UBaseType_t queueLengthOverride,
    TaskHandle_t* outTaskHandle
) {
    const std::size_t index = toIndex(taskName);
    const TaskDefinition& taskDefinition = definition(taskName);

    if (!taskDefinition.supportsQueue || queueItemSize == 0) {
        ESP_LOGE("TaskUtilities", "Task %s does not support a queue or queue item size is zero", taskDefinition.taskLabel);
        return pdFAIL;
    }

    const UBaseType_t queueLength =
        (queueLengthOverride > 0) ? queueLengthOverride : taskDefinition.defaultQueueLength;

    if (queueLength == 0) {
        ESP_LOGE("TaskUtilities", "Queue length for task %s is zero", taskDefinition.taskLabel);
        return pdFAIL;
    }

    QueueHandle_t createdQueueHandle = xQueueCreate(queueLength, queueItemSize);
    if (createdQueueHandle == nullptr) {
        ESP_LOGE("TaskUtilities", "Failed to create queue for task %s", taskDefinition.taskLabel);
        return pdFAIL;
    }

    s_queueHandles[index] = createdQueueHandle;

    const BaseType_t taskCreateResult = createTask(taskName, taskFunction, taskParam, outTaskHandle);
    if (taskCreateResult != pdPASS) {
        ESP_LOGE("TaskUtilities", "Failed to create task %s, deleting its queue", taskDefinition.taskLabel);
        vQueueDelete(createdQueueHandle);
        s_queueHandles[index] = nullptr;
        return pdFAIL;
    }

    return pdPASS;
}

QueueHandle_t queueHandle(TaskNames taskName) {
    return s_queueHandles[toIndex(taskName)];
}

TaskHandle_t taskHandle(TaskNames taskName) {
    return s_taskHandles[toIndex(taskName)];
}

BaseType_t sendToQueue(TaskNames taskName, const void* item, TickType_t ticksToWait) {
    QueueHandle_t handle = queueHandle(taskName);
    if (handle == nullptr) {
        return pdFAIL;
    }

    return xQueueSend(handle, item, ticksToWait);
}

BaseType_t waitOnQueue(TaskNames taskName, void* outItem, TickType_t ticksToWait) {
    QueueHandle_t handle = queueHandle(taskName);
    if (handle == nullptr || outItem == nullptr) {
        return pdFAIL;
    }

    return xQueueReceive(handle, outItem, ticksToWait);
}

}  // namespace TaskUtilities
