#include "ClientInfo.h"
#include "esp_log.h"
#include "esp_debug_helpers.h"

static const char* TAG = "ClientInfo";
ClientInfo::ClientInfo()
{

}

ClientInfo::ClientInfo(int fd, const std::string& ident, ClientInfo::ClientType t)
    : sockfd(fd),
      id(ident),
      type(t),
      lastSeen(std::chrono::steady_clock::now()),
      pingSent(false),
      lastSentDebugMsgCtr(0),
      webClientLastReceivedDebugMsgCtr(0),
      debugMessagesInSync(false),
      lastSentMasterDebugMsgCtr(0)
{
    //ESP_LOGW(TAG, "ClientInfo ctor fd=%d, id=%s type=%s lastSentMasterDebugMsgCtr=%u", sockfd, id.c_str(), ClientInfo::typeToString(type), lastSentMasterDebugMsgCtr);
    // Print the call stack
    //esp_backtrace_print(10);   // print up to 10 stack frames
}

std::chrono::steady_clock::time_point ClientInfo::getLastSeenTime() const
{
    return lastSeen;
}

std::chrono::steady_clock::time_point ClientInfo::getLastPongRxTime() const
{
    return lastPongRxTime;
}
void ClientInfo::updateLastPongRxTime()
{
    lastPongRxTime = std::chrono::steady_clock::now();
}
void ClientInfo::updateLastSeen()
{
    lastSeen = std::chrono::steady_clock::now();
}
void ClientInfo::updateLastSentMasterDebugMsgCtr(uint32_t msgCtr)
{
    lastSentMasterDebugMsgCtr = msgCtr;
    //ESP_LOGW(TAG, "Updating fd=%d, id=%s lastSentMasterDebugMsgCtr=%u", sockfd, id.c_str(), lastSentMasterDebugMsgCtr);
}
uint32_t ClientInfo::getLastSentMasterDebugMsgCtr() const {
    return lastSentMasterDebugMsgCtr;
}