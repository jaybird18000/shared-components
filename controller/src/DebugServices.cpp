#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "DebugServices.h"
#include "DeviceConfig.h"
#include "wsServer.h"
#include "clientsList.h"
#include "TaskUtilities.h"
#include "ConsoleApp.h"
#include "esp_log.h"
#include <cstdarg>
#include <mutex>
#include <vector>

namespace debugServices {
    std::deque<DebugEntry> debugLog_;
    uint32_t debugMsgCounter_;
    static const char* TAG = "DebugServices";
    static std::mutex debugMutex_;
}

void debugServices::init()
{
    debugMsgCounter_ = 1;
}

void debugServices::postDebug(const std::string &message)
{
    uint32_t id = 0;
    std::string json;

    {
        std::lock_guard<std::mutex> lock(debugMutex_);

        // Assign ID
        id = debugMsgCounter_++;

        // Determine if message already starts with "S:" or "M:"
        bool hasPrefix =
            (message.rfind("S:", 0) == 0) ||
            (message.rfind("M:", 0) == 0) ||
            (message.rfind("SS:", 0) == 0);

        // Build final message string
        std::string msg;
        if (hasPrefix) {
            msg = message;   // leave as-is
        } else {
            switch (DeviceConfig::instance().getDeviceRole()) {
                case DeviceRole::MASTER:
                    msg = "M: " + message;
                    break;
                case DeviceRole::SLAVE:
                    msg = "S: " + message;
                    break;
                case DeviceRole::SUNSHADE:
                    msg = "SS: " + message;
                    break;
                default:
                    break;
            }
        }

        // Build the full JSON string ONCE
        json =
            std::string("{\"type\":\"debug\",\"id\":") +
            std::to_string(id) +
            ",\"message\":\"" + msg + "\"}";

        // Store the FULL JSON string in the log
        debugLog_.push_back({id, json});
        while (debugLog_.size() > 500) {
            debugLog_.pop_front();
        }
    }

    // Broadcast the SAME JSON string
    broadcastDebugText(id, json, AudienceType::BROWSERS);
}
void debugServices::postDebug(const char* fmt, ...)
{
    char buffer[256];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Reuse your existing overload
    postDebug(std::string(buffer));
}

// this function sends the debug message to the web clients (browser)
// It will also forward the debug messaage to the master if we are the slave 
void debugServices::broadcastDebugText(uint32_t id, const std::string& msg, AudienceType audience)
{
    ClientsList::instance().forEachClient([&](int fd, ClientInfo& info) 
    {

        // send to master if we are the slave
        if (info.type == ClientInfo::ClientType::MASTER)
        {
//            ESP_LOGW(TAG, "Slave forwarding debug message to master fd=%d, id=%u %s", fd, id, msg.c_str());

            // send message to the slave controller command queue so it can forward to the master
            if(TaskUtilities::queueHandle(TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK) != nullptr)
            {
                ESP_LOGW(TAG, "Slave forwarding dbgMsg to master via r33ClientControllerMessage queue id %d lastIdSent %d", id, info.getLastSentMasterDebugMsgCtr());
                if((id - info.getLastSentMasterDebugMsgCtr()) > 1)
                {
                    ESP_LOGW(TAG, "Client syncing debug msgs with master");
                    int lastIdSent = sendDebugMsgsSinceToR33ClientControllerMessageQueue(info.getLastSentMasterDebugMsgCtr());
                    if (lastIdSent > 0)
                    {
                        ESP_LOGW(TAG, "Failed to send debug msgs to R33 client controller message queue lastIdSent=%d", lastIdSent);
                        info.updateLastSentMasterDebugMsgCtr(lastIdSent);

                    }
                    else
                    {
                        info.updateLastSentMasterDebugMsgCtr(id);
                    }
                }
                else
                {
                    sendMsgToR33ClientControllerMessageQueue(msg);
                    info.updateLastSentMasterDebugMsgCtr(id);
                }

            } 
            else 
            {
                ESP_LOGW(TAG, "No r33ClientControllerMessageTask queue set — cannot forward message");
            }   
        } 
        else if ((info.type == ClientInfo::ClientType::BROWSER) &&
                (audience == AudienceType::BROWSERS || audience == AudienceType::BOTH)) 
        {
            std::string me = "";
            switch (DeviceConfig::instance().getDeviceRole()) {
                case DeviceRole::MASTER:
                    me += "MASTER - ";
                    break;
                case DeviceRole::SLAVE:
                    me += "SLAVE - ";
                    break;
                case DeviceRole::SUNSHADE:
                    me += "SUNSHADE - ";
                    break;
                default:
                    me += "UNKNOWN";
                    break;
            }

            if(info.debugMessagesInSync)
            {
//                ESP_LOGW(TAG, "%s sending msg to browser fd=%d, id=%u", me.c_str(), fd, id);

                WsServer::instance().sendTextMsg(fd, msg);
                // update the last sent debug message counter for this client
                info.lastSentDebugMsgCtr = id;
            }
            else
            {
                ESP_LOGW(TAG, "%s Browser Client fd=%d not in sync, skipping debug message id=%u", me.c_str(), fd, id);
            }

        }

    });
}

void debugServices::sendDebugMsgsSince(int msgCtr, int sockfd)
{
    std::vector<DebugEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(debugMutex_);
        snapshot.assign(debugLog_.begin(), debugLog_.end());
    }
    int lastSentId = 0;
    // send all debug messages with id > msgCtr to the specified sockfd
    for (const auto& entry : snapshot) {
        if (entry.id > msgCtr) {
            WsServer::instance().sendTextMsg(sockfd, entry.message);

            //ESP_LOGW(TAG, "Syncing: Sending debug message to browser client fd=%d, id=%u", sockfd, entry.id);
            lastSentId = entry.id;
            vTaskDelay(pdMS_TO_TICKS(25));   // 20–30 ms is enough
        }
    }
    ESP_LOGW(TAG, "Syncing: Sent debug messages %d to %d to browser client fd=%d", msgCtr, lastSentId, sockfd);
    ClientsList::instance().findClient(sockfd)->debugMessagesInSync = true;
}

int debugServices::sendDebugMsgsSinceToR33ClientControllerMessageQueue(int msgCtr)
{
    int result = 0;
    int lastSentId = 0;
    std::vector<DebugEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(debugMutex_);
        snapshot.assign(debugLog_.begin(), debugLog_.end());
    }

    // send all debug messages with id > msgCtr to the slave controller command queue
    for (const auto& entry : snapshot) {
        if (entry.id > msgCtr) {
            if (!sendMsgToR33ClientControllerMessageQueue(entry.message)) {
                result = entry.id;
                break;
            }

            //ESP_LOGW(TAG, "Slave Syncing Master: Sending debug message to slave controller command queue id=%u", entry.id);
            lastSentId = entry.id;
            vTaskDelay(pdMS_TO_TICKS(5));   // 5–10 ms is enough
        }
    }
    ESP_LOGW(TAG, "Syncing: Sent debug messages %d to %d to r33ClientControllerMessageTask queue", msgCtr, lastSentId);
    return result;
}

void debugServices::debugPrintAll(const char* tag, bool toLog, bool toConsole)
{
    std::vector<DebugEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(debugMutex_);
        snapshot.assign(debugLog_.begin(), debugLog_.end());
    }

    char header[64];
    snprintf(header, sizeof(header), "----- DebugServices Log (%u entries) -----", (unsigned)snapshot.size());
    if (toLog) ESP_LOGW(tag, "%s", header);
    if (toConsole) ConsoleApp::instance().writeToConsole(header);

    for (const auto& entry : snapshot) {
        char line[300];
        snprintf(line, sizeof(line), "id=%-6lu %s", entry.id, entry.message.c_str());

        if (toLog) ESP_LOGW(tag, "%s", line);
        if (toConsole) ConsoleApp::instance().writeToConsole(line);
    }

    char footer[64];
    snprintf(footer, sizeof(footer), "--------debugMsgCounter_ %lu--------", debugMsgCounter_);
    if (toLog) ESP_LOGW(tag, "%s", footer);
    if (toConsole) ConsoleApp::instance().writeToConsole(footer);
}
void debugServices::debugPrintCounters(const char* tag, bool toLog, bool toConsole)
{
    char header[64];
    snprintf(header, sizeof(header), "----- DebugServices Log (%u entries) -----", (unsigned)debugLog_.size());
    if (toLog) ESP_LOGW(tag, "%s", header);
    if (toConsole) ConsoleApp::instance().writeToConsole(header);

    char footer[64];
    snprintf(footer, sizeof(footer), "------debugMsgCounter_ %lu ----------", debugMsgCounter_);
    if (toLog) ESP_LOGW(tag, "%s", footer);
    if (toConsole) ConsoleApp::instance().writeToConsole(footer);
}

bool debugServices::sendMsgToR33ClientControllerMessageQueue(const std::string &msg)
{
    bool result = true;
    if(TaskUtilities::queueHandle(TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK) != nullptr)
    {
        //ESP_LOGW(TAG, "Sending debug message to slave controller command queue: %s", msg.c_str());
        TaskUtilities::MsgItem item{};
        item.sockfd = 1;
        item.fromClient = true;
        item.fromBrowser = false;
        item.fromTask = TaskUtilities::TaskNames::No_TASK_NAME;
        item.toTask = TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK;
        item.frameType = httpd_ws_type_t(HTTPD_WS_TYPE_TEXT);
        item.msgType = TaskUtilities::MsgTypes::DEBUG;
        size_t copyLen = std::min(
            static_cast<size_t>(msg.size()),
            sizeof(item.data) - 1
        );

        memcpy(item.data, msg.c_str(), copyLen);
        item.data[copyLen] = '\0';
        item.len = copyLen;
        if (TaskUtilities::sendToQueue(TaskUtilities::TaskNames::R33_CLIENT_CONTROLLER_MESSAGE_TASK, &item, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "R33 Client Controller Message queue full, dropping message");
            result = false;
        }
    } 
    else 
    {
        ESP_LOGW(TAG, "No R33 Client Controller Message queue set — cannot forward message");
        result = false;
    }  
    return result;
}
