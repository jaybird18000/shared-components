#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "esp_http_server.h"

namespace TaskUtilities {
	
enum class TaskNames : std::uint8_t {
	No_TASK_NAME = 0,
	CONSOLE_APP_TASK,
	WS_SERVER_TASK,
	WS_SERVER_MGR_TASK,
	WS_SERVER_MGR_PING_TASK,
	WEB_CLIENT_WORKER_TASK,
	SLAVE_CLIENT_WORKER_TASK,
	BROWSER_CONTROLLER_MESSAGE_TASK,
	R33_CLIENT_CONTROLLER_TASK,
	R33_CLIENT_CONTROLLER_SOCKET_TASK,
	R33_CLIENT_CONTROLLER_MESSAGE_TASK,
	R33_MASTER_CONTROLLER_TASK,
	R33_SLAVE_CONTROLLER_MESSAGE_TASK,
	R33_SUNSHADE_CONTROLLER_TASK,
	R33_SUNSHADE_CONTROLLER_MESSAGE_TASK,
	AC_UPDATE_TASK,
	BROADCAST_STATUS_TASK,
	OTA_SERVER_TASK,
	OTA_MANAGER_TASK,
	Count
};

enum class MsgTypes : std::uint8_t {
	NONE = 0,
	DEBUG,
	REGISTRATION,
	DATA,
	Count
};

struct TaskDefinition {
	TaskNames taskName;
	const char* taskLabel;
	const char* queueName;
	UBaseType_t priority;
	std::uint32_t stackDepth;
	UBaseType_t defaultQueueLength;
	bool supportsQueue;
};

class MsgItem {
public:
    int sockfd;
    bool fromClient;
	bool fromMaster;
    bool fromBrowser;
	TaskNames fromTask;
	TaskNames toTask;	

	MsgTypes msgType;
    httpd_ws_type_t frameType;
    uint16_t len;
    char data[256];

    MsgItem() : 
	sockfd(-1), 
	fromClient(false), 
	fromMaster(false),
	fromBrowser(false), 
	fromTask(TaskNames::No_TASK_NAME), 
	toTask(TaskNames::No_TASK_NAME), 
	msgType(MsgTypes::NONE), 
	frameType(HTTPD_WS_TYPE_TEXT), 
	len(0) 
	{
        data[0] = '\0';
    }

    void setMsg(const std::string& msg) {
        // Copy safely into the fixed buffer
        snprintf(data, sizeof(data), "%s", msg.c_str());
        len = strlen(data);
    }

    void setMsg(const char* msg) {
        snprintf(data, sizeof(data), "%s", msg);
        len = strlen(data);
    }
    // ---------------------------------------------------------
    // ⭐ Serializer: convert MsgItem → byte vector
    // ---------------------------------------------------------
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf;
        buf.reserve(300);

        auto push = [&](auto value) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
            buf.insert(buf.end(), p, p + sizeof(value));
        };

        push(sockfd);
        push(fromClient);
        push(fromMaster);
        push(fromBrowser);
        push(fromTask);
        push(toTask);
        push(msgType);
        push(frameType);
        push(len);

        buf.insert(buf.end(), data, data + len);

        return buf;
    }

    // ---------------------------------------------------------
    // ⭐ Deserializer: populate class items with buffer 
    // ---------------------------------------------------------
    bool deserialize(const uint8_t* buf, size_t size) {
        size_t offset = 0;

        auto pull = [&](auto& value) {
            if (offset + sizeof(value) > size) return false;
            memcpy(&value, buf + offset, sizeof(value));
            offset += sizeof(value);
            return true;
        };

        if (!pull(sockfd)) return false;
        if (!pull(fromClient)) return false;
        if (!pull(fromMaster)) return false;
        if (!pull(fromBrowser)) return false;
        if (!pull(fromTask)) return false;
        if (!pull(toTask)) return false;
        if (!pull(msgType)) return false;
        if (!pull(frameType)) return false;
        if (!pull(len)) return false;

        if (len > sizeof(data)) return false;
        if (offset + len > size) return false;

        memcpy(data, buf + offset, len);
        data[len] = '\0';

        return true;
    }

	void print() const 
	{
		ESP_LOGI("MsgItem", "----- MsgItem -----");
		ESP_LOGI("MsgItem", "sockfd: %d", sockfd);
		ESP_LOGI("MsgItem", "fromClient: %s", fromClient ? "true" : "false");
		ESP_LOGI("MsgItem", "fromMaster: %s", fromMaster ? "true" : "false");
		ESP_LOGI("MsgItem", "fromBrowser: %s", fromBrowser ? "true" : "false");

		ESP_LOGI("MsgItem", "fromTask: %d", static_cast<int>(fromTask));
		ESP_LOGI("MsgItem", "toTask: %d", static_cast<int>(toTask));

		ESP_LOGI("MsgItem", "msgType: %d", static_cast<int>(msgType));
		ESP_LOGI("MsgItem", "frameType: %d", static_cast<int>(frameType));

		ESP_LOGI("MsgItem", "len: %u", len);
		ESP_LOGI("MsgItem", "data: %s", data);
		ESP_LOGI("MsgItem", "-------------------");
	}	
};

// *******************************************
// NOTE: The following array must be defined in the same order as the TaskName enum.
// Each entry corresponds to a task's definition.
// *******************************************
inline constexpr std::array<TaskDefinition, static_cast<std::size_t>(TaskNames::Count)> TaskInfoByName = {{
	{TaskNames::No_TASK_NAME, "NoTask", "", 0, 4096, 0, false},
	{TaskNames::CONSOLE_APP_TASK, "ConsoleApprTask", "console_app_queue", 5, 4096, 20, true},
	{TaskNames::WS_SERVER_TASK, "WsServerTask", "ws_server_queue", 5, 4096, 20, true},
	{TaskNames::WS_SERVER_MGR_TASK, "WsServerMgrTask", "ws_server_mgr_queue", 5, 4096, 20, true},
	{TaskNames::WS_SERVER_MGR_PING_TASK, "WsServerMgrPing", "", 4, 4096, 0, false},
	{TaskNames::WEB_CLIENT_WORKER_TASK, "WebClientServerMgrWorker", "web_client_worker_queue", 4, 4096, 20, true},
	{TaskNames::SLAVE_CLIENT_WORKER_TASK, "SlaveClientServerMgrWorker", "slave_client_worker_queue", 4, 4096, 20, true},	
	{TaskNames::BROWSER_CONTROLLER_MESSAGE_TASK, "BrowserControllerMessage", "browser_controller_message_queue", 4, 4096, 20, true},
	{TaskNames::R33_CLIENT_CONTROLLER_TASK, "R33ClientController", "r33_client_controller_queue", 4, 4096, 20, true},
	{TaskNames::R33_CLIENT_CONTROLLER_SOCKET_TASK, "R33ClientControllerSocket", "r33_client_controller_socket_queue", 4, 4096, 20, true},
	{TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK, "R33ClientControllerMessage", "r33_client_controller_message_queue", 4, 4096, 20, true},
	{TaskNames::R33_MASTER_CONTROLLER_TASK, "R33MasterController", "r33_master_controller_queue", 4, 4096, 20, true},
	{TaskNames::R33_SLAVE_CONTROLLER_MESSAGE_TASK, "R33SlaveControllerMessage", "r33_slave_controller_message_queue", 4, 4096, 20, true},
	{TaskNames::R33_SUNSHADE_CONTROLLER_TASK, "R33SunshadeController", "r33_sunshade_controller_queue", 4, 4096, 20, true},
	{TaskNames::R33_SUNSHADE_CONTROLLER_MESSAGE_TASK, "R33SunshadeControllerMessage", "r33_sunshade_controller_message_queue", 4, 4096, 20, true},
	{TaskNames::AC_UPDATE_TASK, "WsServerMgrAcUpdate", "ac_update_queue", 4, 4096, 20, true},
	{TaskNames::BROADCAST_STATUS_TASK, "WsBroadcastStatus", "", 4, 4096, 0, false},
	{TaskNames::OTA_SERVER_TASK, "OtaTask", "", 5, 4096, 0, false},
	{TaskNames::OTA_MANAGER_TASK, "OtaUpdateTask", "", 5, 4096, 0, false},
}};

constexpr std::size_t toIndex(TaskNames taskName) {
	return static_cast<std::size_t>(taskName);
}

const TaskDefinition& definition(TaskNames taskName);

BaseType_t createTask(
	TaskNames taskName,
	TaskFunction_t taskFunction,
	void* taskParam = nullptr,
	TaskHandle_t* outTaskHandle = nullptr
);

BaseType_t createTaskWithQueue(
	TaskNames taskName,
	TaskFunction_t taskFunction,
	std::size_t queueItemSize,
	void* taskParam = nullptr,
	UBaseType_t queueLengthOverride = 0,
	TaskHandle_t* outTaskHandle = nullptr
);

QueueHandle_t queueHandle(TaskNames taskName);
TaskHandle_t taskHandle(TaskNames taskName);

BaseType_t sendToQueue(TaskNames taskName, const void* item, TickType_t ticksToWait = 0);

BaseType_t waitOnQueue(TaskNames taskName, void *outItem, TickType_t ticksToWait);

}  // namespace TaskUtilities

