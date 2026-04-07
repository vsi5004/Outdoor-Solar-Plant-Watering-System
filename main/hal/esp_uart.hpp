#pragma once
#include "hal/iuart.hpp"
#include "driver/uart.h"

// IUart backed by the ESP-IDF UART driver.
class EspUart : public IUart {
public:
    // port     — UART_NUM_0 … UART_NUM_1
    // txPin    — GPIO number for TX
    // rxPin    — GPIO number for RX
    // baudRate — bits per second (e.g. 9600)
    EspUart(uart_port_t port, int txPin, int rxPin, uint32_t baudRate);

    void   write(const uint8_t* data, size_t len)              override;
    size_t read(uint8_t* buf, size_t len, uint32_t timeoutMs)  override;
    void   flushRx()                                           override;

private:
    uart_port_t port_;

    static constexpr int kRxBufSize = 256;
};
