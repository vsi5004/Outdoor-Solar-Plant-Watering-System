#pragma once
#include <cstddef>
#include <cstdint>

// Byte-stream UART interface used by the Renogy Modbus driver.
// Timeout values are in milliseconds; implementations convert to RTOS ticks.
class IUart {
public:
    virtual ~IUart() = default;

    // Transmit len bytes from data.
    virtual void write(const uint8_t* data, size_t len) = 0;

    // Receive up to len bytes into buf, waiting at most timeoutMs.
    // Returns the number of bytes actually read (may be less than len).
    virtual size_t read(uint8_t* buf, size_t len, uint32_t timeoutMs) = 0;

    // Discard any bytes waiting in the RX buffer.
    // Call before sending a new Modbus request to clear stale data.
    virtual void flushRx() = 0;
};
