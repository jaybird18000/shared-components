#include "DebugCommands.h"
#include "DebugServices.h"
#include "ConsoleApp.h"
#include "ClientsList.h"
#include <string>
#include "esp_log.h"

static const char* TAG = "dbCmds";

void debugCommands::handle_command(const char *cmd)
{
    //ESP_LOGI(TAG, "handleCommand: %s", cmd);

    if (strcmp(cmd, "status") == 0) {
        ConsoleApp::instance().writeToConsole("System OK");
        //uart_write_bytes(UART_NUM_0, "System OK\r\n", 11);
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        ConsoleApp::instance().writeToConsole("Commands:");
        ConsoleApp::instance().writeToConsole("   status - Display system status");
        ConsoleApp::instance().writeToConsole("   help   - Display this help message");
        ConsoleApp::instance().writeToConsole("   dcl    - Display Clients List to cponsole only");
        ConsoleApp::instance().writeToConsole("   dcl-b  - Display Clients List to the log and console");
        ConsoleApp::instance().writeToConsole("   dclc   - Display Clients List counters to console only");
        ConsoleApp::instance().writeToConsole("   dclc-b - Display Clients List counters to the log and console");
        ConsoleApp::instance().writeToConsole("   ddl    - Display Debug Services log to console only");
        ConsoleApp::instance().writeToConsole("   ddl-b  - Display Debug Services log to the log and console");
        ConsoleApp::instance().writeToConsole("   ddlc   - Display Debug Services log counters to console only");
        ConsoleApp::instance().writeToConsole("   ddlc-b - Display Debug Services log counters to the log and console");
        ConsoleApp::instance().writeToConsole("   dbgctl - List debug control items and toggle one by number");

        return;
    }

    if (strcmp(cmd, "dcl") == 0) {
        ConsoleApp::instance().writeToConsole("display clients list");
        ClientsList::instance().debugPrintAll("ClientsList", false, true);
        return;
    }
    if (strcmp(cmd, "dcl-b") == 0) {
        ConsoleApp::instance().writeToConsole("display clients list");
        ClientsList::instance().debugPrintAll("ClientsList", true, true);
        return;
    }
    if (strcmp(cmd, "dclc") == 0) {
        ConsoleApp::instance().writeToConsole("display clients list counters");
        ClientsList::instance().debugPrintCounters("ClientsList", false, true);
        return;
    }
    if (strcmp(cmd, "dclc-b") == 0) {
        ConsoleApp::instance().writeToConsole("display clients list counters");
        ClientsList::instance().debugPrintCounters("ClientsList", true, true);
        return;
    }
    if (strcmp(cmd, "ddl") == 0) {
        ConsoleApp::instance().writeToConsole("display debug services log");
        debugServices::debugPrintAll("DebugServices", false, true);
        return;
    }
    if (strcmp(cmd, "ddl-b") == 0) {
        ConsoleApp::instance().writeToConsole("display debug services log");
        debugServices::debugPrintAll("DebugServices", true, true);
        return;
    }
    if (strcmp(cmd, "ddlc") == 0) {
        ConsoleApp::instance().writeToConsole("display debug services log counters");
        debugServices::debugPrintCounters("DebugServices", false, true);
        return;
    }
    if (strcmp(cmd, "ddlc-b") == 0) {
        ConsoleApp::instance().writeToConsole("display debug services log counters");
        debugServices::debugPrintCounters("DebugServices", true, true);
        return;
    }
    if (strcmp(cmd, "dbgctl") == 0) {
        ConsoleApp::instance().printDebugControlList();
        return;
    }
    ConsoleApp::instance().writeToConsole("Unknown command");
    //uart_write_bytes(UART_NUM_0, "Unknown command\r\n", 17);
}