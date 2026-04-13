#include "hal/esp_uart.hpp"
#include "esp_err.h"
#include "esp_log.h"

static const char* TAG = "EspUart";

EspUart::EspUart(uart_port_t port, int txPin, int rxPin, uint32_t baudRate)
    : port_(port)
{
    uart_config_t cfg = {
        .baud_rate           = static_cast<int>(baudRate),
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(port_, kRxBufSize, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(port_, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(port_, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void EspUart::write(const uint8_t* data, size_t len)
{
    const int written = uart_write_bytes(port_, data, len);
    if (written < 0 || static_cast<size_t>(written) != len) {
        ESP_LOGE(TAG, "uart_write_bytes: requested %u wrote %d",
                 static_cast<unsigned>(len), written);
    }
}

size_t EspUart::read(uint8_t* buf, size_t len, uint32_t timeoutMs)
{
    const TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
    const int n = uart_read_bytes(port_, buf, static_cast<uint32_t>(len), ticks);
    return (n > 0) ? static_cast<size_t>(n) : 0u;
}

void EspUart::flushRx()
{
    uart_flush_input(port_);
}
