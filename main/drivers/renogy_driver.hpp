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

    // Enable or disable the controller's 12V load output (register 0x010A).
    // Returns true if the controller echoed a valid acknowledgement.
    bool setLoad(bool on) override;

    // ── Utilities exposed for testing ────────────────────────────────────────

    // CRC-16/IBM (Modbus): polynomial 0xA001, init 0xFFFF.
    static uint16_t crc16(const uint8_t* buf, size_t len);

    // Fill buf[8] with a valid Modbus read-holding-registers (FC 0x03) frame.
    static void buildRequest(uint8_t* buf, uint8_t addr,
                             uint16_t startReg, uint8_t count);

    // Fill buf[8] with a valid Modbus write-single-register (FC 0x06) frame.
    static void buildWriteRequest(uint8_t* buf, uint8_t addr,
                                  uint16_t reg, uint16_t value);

    // Read a single register and return its value. Returns false on timeout or
    // CRC error. Used for diagnostic readbacks in target tests.
    bool readSingleRegister(uint16_t reg, uint16_t& out);

private:
    // Send a read request and populate out[0..count-1] on success.
    bool readRegisters(uint16_t startReg, uint8_t count, uint16_t* out);

    // Send a write-single-register request and validate the echo response.
    bool writeRegister(uint16_t reg, uint16_t value);

    // Validate and unpack a Modbus FC03 response into out[0..expectedCount-1].
    static bool parseResponse(const uint8_t* buf, size_t len,
                              uint8_t addr, uint8_t expectedCount,
                              uint16_t* out);

    IUart&            uart_;
    SemaphoreHandle_t mutex_;
    RenogyData        data_{};
};
