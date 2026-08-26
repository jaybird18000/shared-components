#pragma once
#include <string>

class ConsoleApp
{
    public:

        static ConsoleApp& instance();
        void start_console();
        void init_uart_console();
        static void console_task(void* arg);
        void handle_command(const char* cmd);
        void writeToConsole(const std::string msg);
        void printDebugControlList();
        bool handleDebugControlInput(const char* input);

    private:
        bool awaitingDebugToggle_ = false;
};