#ifndef CLIENT_INFO_H
#define CLIENT_INFO_H

#include <string>
#include <chrono>

class ClientInfo {

    public:
        // ---- NEW ENUM TYPE ----
        enum class ClientType {
            BROWSER,
            SLAVE,
            MASTER,
            SUNSHADE
        };

        // ---- STATIC HELPER TO CONVERT ENUM TO STRING ----
        static const char* typeToString(ClientType t) {
            switch (t) {
                case ClientType::BROWSER: return "browser";
                case ClientType::SLAVE:   return "slave";
                case ClientType::MASTER:  return "master";
                case ClientType::SUNSHADE: return "sunshade";
                default:                  return "unknown";
            }
        }

        // ---- DATA MEMBERS ----
        int sockfd = -1;
        std::string id;                 // "slave-1", "browser-12", etc.
        ClientType type = ClientType::BROWSER;

        std::chrono::steady_clock::time_point lastSeen;
        std::chrono::steady_clock::time_point lastPongRxTime;
        bool pingSent = false;
        uint32_t lastSentDebugMsgCtr;  // last debug message sent to this client
        //uint32_t lastSentMasterDebugMsgCtr;  // last debug message sent to the master client
        uint32_t webClientLastReceivedDebugMsgCtr; // last debug message received from this client
        bool debugMessagesInSync = false; // whether the client has received all debug messages up to lastSentDebugMsgCtr

        // ---- DEFAULT CONSTRUCTOR ----
        ClientInfo();

        // ---- UPDATED CONSTRUCTOR ----
        ClientInfo(int fd, const std::string& ident, ClientInfo::ClientType t);

        std::chrono::steady_clock::time_point getLastSeenTime() const;
        std::chrono::steady_clock::time_point getLastPongRxTime() const;
        void updateLastPongRxTime();
        void updateLastSeen();
        void updateLastSentMasterDebugMsgCtr(uint32_t msgCtr);
        uint32_t getLastSentMasterDebugMsgCtr() const ;

    private:
        uint32_t lastSentMasterDebugMsgCtr;  // last debug message sent to the master client
};

#endif // CLIENT_INFO_H