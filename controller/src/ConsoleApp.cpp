#include "ConsoleApp.h"
#include "TaskUtilities.h"
#include "DebugCommands.h"
#include "DebugControl.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <cstdlib>

static const char* TAG = "console";

ConsoleApp& ConsoleApp::instance() {
    static ConsoleApp inst;
    return inst;
}
void ConsoleApp::init_uart_console()
{
const uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,

    // Required by your driver
    .rx_flow_ctrl_thresh = 0,

    // Use the normal UART clock
    .source_clk = UART_SCLK_DEFAULT,

    // Required bitfields
    .flags = {
        .allow_pd = 0,
        .backup_before_sleep = 0
    }
};

    // Install UART driver: RX buffer = 256 bytes, TX buffer = 0 (not needed)
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));

    // Apply the UART configuration
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));

    // Use default pins (because UART0 is already wired internally)
    ESP_ERROR_CHECK(uart_set_pin(
        UART_NUM_0,
        43,
        44,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    ));

}

void ConsoleApp::start_console()
{
    ESP_LOGI(TAG, "Starting console...");
    awaitingDebugToggle_ = false;
    init_uart_console();

    BaseType_t ok2 = TaskUtilities::createTaskWithQueue(
        TaskUtilities::TaskNames::CONSOLE_APP_TASK,
        &console_task,
        sizeof(char *),
        this
    );
}

void ConsoleApp::console_task(void* arg)
{
    ESP_LOGI(TAG, "console_task started");
    ConsoleApp* self = static_cast<ConsoleApp*>(arg);

    uart_flush_input(UART_NUM_0);
    static char line_buf[128];
    int pos = 0;

    // Print initial prompt
    const char* prompt = "> ";
    uart_write_bytes(UART_NUM_0, prompt, strlen(prompt));

    while (true)
    {
        uint8_t ch;
        int n = uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY);
        if (n <= 0) {
            continue;
        }
        //ESP_LOGI(TAG, "console_task read char 0x%02X ('%c')", ch, (ch >= 32 && ch <= 126) ? ch : '.');
        // Handle CR/LF as end-of-line
        if (ch == '\r') continue;

        if (ch == '\n') {
            uart_write_bytes(UART_NUM_0, "\r\n", 2);

            line_buf[pos] = '\0';
            if (pos > 0) {
                if (self->awaitingDebugToggle_) {
                    self->handleDebugControlInput(line_buf);
                } else {
                    debugCommands::handle_command(line_buf);
                }
                //self->handle_command(line_buf);
                //ESP_LOGI(TAG, "Received command: %s", line_buf);
            }

            // Reset buffer and prompt
            pos = 0;
            uart_write_bytes(UART_NUM_0, prompt, strlen(prompt));
            continue;
        }

        // Simple backspace handling
        if (ch == 0x08 || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                uart_write_bytes(UART_NUM_0, "\b \b", 3);
            }
            continue;
        }

        // Normal character
        if (pos < (int)sizeof(line_buf) - 1) {
            line_buf[pos++] = (char)ch;
            uart_write_bytes(UART_NUM_0, (const char*)&ch, 1);
        }
    }
}
void ConsoleApp::handle_command(const char* cmd)
{
    //ESP_LOGI(TAG, "Received command: %s", cmd);

    if (strcmp(cmd, "status") == 0) {
        uart_write_bytes(UART_NUM_0, "System OK\r\n", 11);
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        uart_write_bytes(UART_NUM_0, "Commands: status, help\r\n", 26);
        return;
    }

    uart_write_bytes(UART_NUM_0, "Unknown command\r\n", 17);
}

void ConsoleApp::writeToConsole(const std::string msg)
{
    std::string msg1 = msg + "\r\n";
    uart_write_bytes(UART_NUM_0, msg1.c_str(), msg1.length());
}

void ConsoleApp::printDebugControlList()
{
    writeToConsole("----- Debug Control Items -----");
    for (size_t i = 0; i < static_cast<size_t>(Item::COUNT); ++i) {
        Item item = static_cast<Item>(i);
        char line[64];
        snprintf(line, sizeof(line), "%2u) %-20s [%s]",
                 (unsigned)i,
                 DebugControl::itemToString(item),
                 DebugControl::enabled(item) ? "ON" : "OFF");
        writeToConsole(line);
    }
    writeToConsole("Enter item number to toggle:");
    awaitingDebugToggle_ = true;
}

bool ConsoleApp::handleDebugControlInput(const char* input)
{
    if (!awaitingDebugToggle_) return false;
    awaitingDebugToggle_ = false;

    char* endptr = nullptr;
    long idx = strtol(input, &endptr, 10);
    if (endptr == input || idx < 0 || idx >= static_cast<long>(Item::COUNT)) {
        writeToConsole("Invalid item number");
        return true;
    }

    Item item = static_cast<Item>(idx);
    bool newVal = !DebugControl::enabled(item);
    DebugControl::set(item, newVal);

    char line[64];
    snprintf(line, sizeof(line), "%s is now %s", DebugControl::itemToString(item), newVal ? "ON" : "OFF");
    writeToConsole(line);
    return true;
}
