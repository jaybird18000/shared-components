#ifndef CLIENT_INFO_H
#define CLIENT_INFO_H

#include <string>
#include <chrono>

struct ClientInfo {

    // ---- NEW ENUM TYPE ----
    enum class ClientType {
        BROWSER,
        SLAVE,
        MASTER
    };

    // ---- STATIC HELPER TO CONVERT ENUM TO STRING ----
    static const char* typeToString(ClientType t) {
        switch (t) {
            case ClientType::BROWSER: return "browser";
            case ClientType::SLAVE:   return "slave";
            case ClientType::MASTER:  return "master";
            default:                  return "unknown";
        }
    }

    // ---- DATA MEMBERS ----
    int sockfd = -1;
    std::string id;                 // "slave-1", "browser-12", etc.
    ClientType type = ClientType::BROWSER;

    std::chrono::steady_clock::time_point lastSeen;
    bool pingSent = false;
    uint32_t lastSentDebugMsgCtr = 0;  // last debug message sent to this client
    uint32_t lastSentMasterDebugMsgCtr = 0;  // last debug message sent to the master client
    uint32_t webClientLastReceivedDebugMsgCtr = 0; // last debug message received from this client
    bool debugMessagesInSync = false; // whether the client has received all debug messages up to lastSentDebugMsgCtr

    // ---- DEFAULT CONSTRUCTOR ----
    ClientInfo() = default;

    // ---- UPDATED CONSTRUCTOR ----
    ClientInfo(int fd, const std::string& ident, ClientType t)
        : sockfd(fd),
          id(ident),
          type(t),
          lastSeen(std::chrono::steady_clock::now()),
          pingSent(false),
          lastSentDebugMsgCtr(0),
          lastSentMasterDebugMsgCtr(0),
          webClientLastReceivedDebugMsgCtr(0),
          debugMessagesInSync(false)
    {}
};

#endif // CLIENT_INFO_H