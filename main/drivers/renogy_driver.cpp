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
    // Block 1: 0x0100–0x010F (16 registers)
    uint16_t block1[16] = {};
    if (!readRegisters(0x0100, 16, block1)) {
        return false;
    }

    // Block 2: 0x0300–0x0310 (17 registers)
    uint16_t block2[17] = {};
    if (!readRegisters(0x0300, 17, block2)) {
        return false;
    }

    RenogyData d{};
    d.batterySoc      = block1[0x0101 - 0x0100];
    d.batteryVoltage  = static_cast<float>(block1[0x0102 - 0x0100]) * 0.1f;
    d.chargingCurrent = static_cast<float>(block1[0x0103 - 0x0100]) * 0.01f;
    // Controller temp: high byte of register 0x0107
    d.controllerTemp  = static_cast<int8_t>((block1[0x0107 - 0x0100] >> 8) & 0xFF);
    d.pvVoltage       = static_cast<float>(block2[0x0300 - 0x0300]) * 0.1f;
    d.chargingPower   = block2[0x0302 - 0x0300];
    d.loadCurrent     = static_cast<float>(block2[0x0310 - 0x0300]) * 0.01f;
    d.lastUpdateMs    = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));

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
    buf[6] = static_cast<uint8_t>(crc & 0xFF);  // CRC lo byte
    buf[7] = static_cast<uint8_t>(crc >> 8);     // CRC hi byte
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
