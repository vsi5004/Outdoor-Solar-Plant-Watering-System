#include "drivers/renogy_driver.hpp"
#include "config.hpp"
#include "freertos/task.h"
#include <cassert>
#include <cstring>

// Maximum response frame: 5 header/CRC bytes + 17 registers × 2 bytes = 39
static constexpr size_t MAX_RESPONSE_BYTES = 64;

// ── Construction / destruction ────────────────────────────────────────────────

RenogyDriver::RenogyDriver(IUart& uart)
    : uart_(uart)
    , mutex_(xSemaphoreCreateMutex())
{
    assert(mutex_ != nullptr);
}

RenogyDriver::~RenogyDriver()
{
    vSemaphoreDelete(mutex_);
}

// ── Public API ────────────────────────────────────────────────────────────────

bool RenogyDriver::poll()
{
    // Block 1: real-time telemetry (0x0100–0x0109, 10 registers).
    uint16_t r[10] = {};
    if (!readRegisters(0x0100, 10, r)) {
        return false;
    }

    // Block 2: daily historical data (0x010B–0x0114, 10 registers).
    uint16_t h[10] = {};
    if (!readRegisters(0x010B, 10, h)) {
        return false;
    }

    // Block 3: status register (0x0120, 1 register).
    uint16_t s[1] = {};
    if (!readRegisters(0x0120, 1, s)) {
        return false;
    }

    // Controller temp (0x0103): high byte, bit 7 = sign, bits 6:0 = magnitude °C.
    const uint8_t tempHi  = static_cast<uint8_t>(r[3] >> 8);
    const float   tempMag = static_cast<float>(tempHi & 0x7F);

    RenogyData d{};
    d.valid = true;
    // Real-time
    d.batterySoc     = r[0];                                                    // 0x0100
    d.batteryVoltage = static_cast<float>(r[1]) * 0.1f;                        // 0x0101
    d.batteryCurrent = static_cast<float>(static_cast<int16_t>(r[2])) * 0.01f; // 0x0102 signed
    d.controllerTemp = (tempHi & 0x80) ? -tempMag : tempMag;                   // 0x0103 high byte
    d.loadPower      = r[6];                                                    // 0x0106 W
    d.pvVoltage      = static_cast<float>(r[7]) * 0.1f;                        // 0x0107
    d.pvCurrent      = static_cast<float>(r[8]) * 0.01f;                       // 0x0108
    d.pvPower        = r[9];                                                    // 0x0109
    // Historical
    d.maxChargingPowerToday = h[4];                                             // 0x010F
    d.dailyGenerationWh     = h[8];                                             // 0x0113
    d.dailyConsumptionWh    = h[9];                                             // 0x0114
    // Status
    d.chargingStatus         = static_cast<uint8_t>(s[0] & 0xFF);              // 0x0120 low byte
    d.lastUpdateMs           = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));

    xSemaphoreTake(mutex_, portMAX_DELAY);
    data_ = d;
    xSemaphoreGive(mutex_);

    return true;
}

RenogyData RenogyDriver::getData() const
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const RenogyData copy = data_;
    xSemaphoreGive(mutex_);
    return copy;
}

// ── Static utilities ──────────────────────────────────────────────────────────

uint16_t RenogyDriver::crc16(const uint8_t* buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= static_cast<uint16_t>(buf[i]);
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void RenogyDriver::buildRequest(uint8_t* buf, uint8_t addr,
                                uint16_t startReg, uint8_t count)
{
    buf[0] = addr;
    buf[1] = 0x03; // function code: read holding registers
    buf[2] = static_cast<uint8_t>(startReg >> 8);
    buf[3] = static_cast<uint8_t>(startReg & 0xFF);
    buf[4] = 0x00;
    buf[5] = count;
    const uint16_t crc = crc16(buf, 6);
    buf[6] = static_cast<uint8_t>(crc & 0xFF);
    buf[7] = static_cast<uint8_t>(crc >> 8);
}

void RenogyDriver::buildWriteRequest(uint8_t* buf, uint8_t addr,
                                     uint16_t reg, uint16_t value)
{
    buf[0] = addr;
    buf[1] = 0x06; // function code: write single register
    buf[2] = static_cast<uint8_t>(reg >> 8);
    buf[3] = static_cast<uint8_t>(reg & 0xFF);
    buf[4] = static_cast<uint8_t>(value >> 8);
    buf[5] = static_cast<uint8_t>(value & 0xFF);
    const uint16_t crc = crc16(buf, 6);
    buf[6] = static_cast<uint8_t>(crc & 0xFF);
    buf[7] = static_cast<uint8_t>(crc >> 8);
}

// ── Public API (continued) ────────────────────────────────────────────────────

bool RenogyDriver::readSingleRegister(uint16_t reg, uint16_t& out)
{
    return readRegisters(reg, 1, &out);
}

bool RenogyDriver::setLoad(bool on)
{
    // Register 0xE01D controls the load working mode. The controller defaults
    // to light-control mode (0x00) and ignores writes to the load on/off
    // register (0x010A) unless mode is first set to Manual (0x0F).
    if (!writeRegister(0xE01D, 0x000Fu)) {
        return false;
    }
    return writeRegister(0x010A, on ? 1u : 0u);
}

// ── Private helpers ───────────────────────────────────────────────────────────

bool RenogyDriver::readRegisters(uint16_t startReg, uint8_t count, uint16_t* out)
{
    uint8_t request[8];
    buildRequest(request, config::renogy::MODBUS_ADDR, startReg, count);

    uart_.flushRx();
    uart_.write(request, sizeof(request));

    // Response: addr(1) + fc(1) + byte_count(1) + data(count×2) + crc(2)
    const size_t responseLen = 5u + static_cast<size_t>(count) * 2u;
    uint8_t response[MAX_RESPONSE_BYTES] = {};

    const size_t received = uart_.read(response, responseLen,
                                       config::renogy::RESPONSE_TIMEOUT_MS);
    if (received != responseLen) {
        return false;
    }

    return parseResponse(response, responseLen,
                         config::renogy::MODBUS_ADDR, count, out);
}

bool RenogyDriver::writeRegister(uint16_t reg, uint16_t value)
{
    uint8_t request[8];
    buildWriteRequest(request, config::renogy::MODBUS_ADDR, reg, value);

    uart_.flushRx();
    uart_.write(request, sizeof(request));

    // Normal response: controller echoes the request frame exactly (8 bytes).
    uint8_t response[8] = {};
    const size_t received = uart_.read(response, sizeof(response),
                                       config::renogy::RESPONSE_TIMEOUT_MS);
    if (received != sizeof(response)) {
        return false;
    }

    // Validate CRC and confirm the echo matches what we sent.
    const uint16_t expectedCrc = crc16(response, 6);
    const uint16_t frameCrc    = static_cast<uint16_t>(response[6]) |
                                 (static_cast<uint16_t>(response[7]) << 8);
    if (expectedCrc != frameCrc) {
        return false;
    }

    // Echo must match: same addr, FC, register address, and value.
    return (response[0] == request[0] &&
            response[1] == request[1] &&
            response[2] == request[2] &&
            response[3] == request[3] &&
            response[4] == request[4] &&
            response[5] == request[5]);
}

bool RenogyDriver::parseResponse(const uint8_t* buf, size_t len,
                                 uint8_t addr, uint8_t expectedCount,
                                 uint16_t* out)
{
    // Minimum viable frame: addr + fc + byte_count + 2 data bytes + 2 CRC
    if (len < 7) return false;

    if (buf[0] != addr)   return false; // wrong device address
    if (buf[1] != 0x03)   return false; // wrong function code
    if (buf[2] != static_cast<uint8_t>(expectedCount * 2u)) return false;

    // Validate CRC over all bytes except the final two
    const uint16_t expectedCrc = crc16(buf, len - 2);
    const uint16_t frameCrc    = static_cast<uint16_t>(buf[len - 2]) |
                                 (static_cast<uint16_t>(buf[len - 1]) << 8);
    if (expectedCrc != frameCrc) return false;

    // Unpack register values (big-endian pairs, starting at byte 3)
    for (uint8_t i = 0; i < expectedCount; i++) {
        out[i] = (static_cast<uint16_t>(buf[3 + i * 2u]) << 8) |
                  static_cast<uint16_t>(buf[4 + i * 2u]);
    }
    return true;
}
