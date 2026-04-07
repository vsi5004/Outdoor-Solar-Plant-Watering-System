#pragma once
#include <cstddef>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/iuart.hpp"
#include "drivers/irenogy_monitor.hpp"
#include "drivers/renogy_data.hpp"

// Polls the Renogy Wanderer 10A solar charge controller over Modbus RTU.
//
// Protocol: 9600 8N1, function code 0x03 (read holding registers).
// Two register blocks are read per poll:
//   Block 1: 0x0100–0x010F (16 registers) — battery and charging data
//   Block 2: 0x0300–0x0310 (17 registers) — PV and load data
//
// Thread safety: getData() returns a mutex-protected copy of the last
// successfully parsed snapshot. poll() is called from a dedicated low-priority
// FreeRTOS task; getData() may be called from any task.
class RenogyDriver : public IRenogyMonitor {
public:
    explicit RenogyDriver(IUart& uart);
    ~RenogyDriver();

    // Send both register-block requests, parse responses, update stored data.
    // Returns true on success; false if a UART timeout or CRC error occurs.
    // On failure the previously stored data is unchanged.
    bool poll();

    // Return a copy of the last successfully parsed data snapshot.
    RenogyData getData() const override;

    // ── Utilities exposed for testing ────────────────────────────────────────

    // CRC-16/IBM (Modbus): polynomial 0xA001, init 0xFFFF.
    static uint16_t crc16(const uint8_t* buf, size_t len);

    // Fill buf[8] with a valid Modbus read-holding-registers request frame.
    static void buildRequest(uint8_t* buf, uint8_t addr,
                             uint16_t startReg, uint8_t count);

private:
    // Send a request for `count` registers starting at `startReg`.
    // Returns true and populates out[0..count-1] on success.
    bool readRegisters(uint16_t startReg, uint8_t count, uint16_t* out);

    // Validate and unpack a Modbus response into out[0..expectedCount-1].
    static bool parseResponse(const uint8_t* buf, size_t len,
                              uint8_t addr, uint8_t expectedCount,
                              uint16_t* out);

    IUart&            uart_;
    SemaphoreHandle_t mutex_;
    RenogyData        data_{};
};
