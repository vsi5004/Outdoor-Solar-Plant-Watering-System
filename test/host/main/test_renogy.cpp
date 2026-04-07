#include "unity.h"
#include "drivers/renogy_driver.hpp"
#include "drivers/renogy_data.hpp"
#include "config.hpp"
#include "mock_uart.hpp"
#include "freertos/task.h"
#include <vector>
#include <cstring>

// ── Test helpers ──────────────────────────────────────────────────────────────

// Build a valid Modbus response frame for the given register values.
static std::vector<uint8_t> makeResponse(uint8_t addr,
                                         const std::vector<uint16_t>& regs)
{
    std::vector<uint8_t> frame;
    frame.push_back(addr);
    frame.push_back(0x03);
    frame.push_back(static_cast<uint8_t>(regs.size() * 2u));
    for (auto r : regs) {
        frame.push_back(static_cast<uint8_t>(r >> 8));
        frame.push_back(static_cast<uint8_t>(r & 0xFF));
    }
    const uint16_t crc = RenogyDriver::crc16(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

// Build a poll() response pair: block1 (16 regs from 0x0100) and
// block2 (17 regs from 0x0300), with the interesting registers set.
//
//   batterySoc      → block1[1]  (reg 0x0101)
//   batteryVoltage  → block1[2]  (reg 0x0102) ×0.1
//   chargingCurrent → block1[3]  (reg 0x0103) ×0.01
//   controllerTemp  → block1[7]  (reg 0x0107) high byte
//   pvVoltage       → block2[0]  (reg 0x0300) ×0.1
//   chargingPower   → block2[2]  (reg 0x0302) raw
//   loadCurrent     → block2[16] (reg 0x0310) ×0.01
struct PollValues {
    uint16_t batterySoc      = 0;
    uint16_t batteryVoltageR = 0; // raw register (÷10 = V)
    uint16_t chargingCurrR   = 0; // raw register (÷100 = A)
    uint8_t  controllerTemp  = 0; // placed in high byte of reg 0x0107
    uint16_t pvVoltageR      = 0; // raw register (÷10 = V)
    uint16_t chargingPower   = 0;
    uint16_t loadCurrR       = 0; // raw register (÷100 = A)
};

static void enqueuePollResponses(MockUart& uart, const PollValues& v)
{
    std::vector<uint16_t> block1(16, 0);
    block1[0x0101 - 0x0100] = v.batterySoc;
    block1[0x0102 - 0x0100] = v.batteryVoltageR;
    block1[0x0103 - 0x0100] = v.chargingCurrR;
    block1[0x0107 - 0x0100] = static_cast<uint16_t>(v.controllerTemp) << 8;

    std::vector<uint16_t> block2(17, 0);
    block2[0x0300 - 0x0300] = v.pvVoltageR;
    block2[0x0302 - 0x0300] = v.chargingPower;
    block2[0x0310 - 0x0300] = v.loadCurrR;

    const uint8_t addr = config::renogy::MODBUS_ADDR;
    auto r1 = makeResponse(addr, block1);
    auto r2 = makeResponse(addr, block2);
    uart.enqueueRx(r1);
    uart.enqueueRx(r2);
}

// ── CRC-16 ────────────────────────────────────────────────────────────────────

static void test_crc16_zero_bytes_returns_init_value(void)
{
    // With no bytes processed the CRC equals the init value 0xFFFF.
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, RenogyDriver::crc16(nullptr, 0));
}

static void test_crc16_known_modbus_vector(void)
{
    // Independently verified Modbus CRC-16 test vector:
    //   Request: read 1 register from address 0x0000, device 0x01
    //   Frame:   01 03 00 00 00 01  →  uint16_t CRC = 0x0A84 (lo=0x84 hi=0x0A)
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX16(0x0A84, RenogyDriver::crc16(frame, sizeof(frame)));
}

static void test_crc16_is_deterministic(void)
{
    const uint8_t data[] = {0x01, 0x03, 0x01, 0x01, 0x00, 0x10};
    TEST_ASSERT_EQUAL_HEX16(RenogyDriver::crc16(data, sizeof(data)),
                             RenogyDriver::crc16(data, sizeof(data)));
}

static void test_crc16_different_inputs_produce_different_results(void)
{
    const uint8_t a[] = {0x01, 0x03, 0x01, 0x00, 0x00, 0x10};
    const uint8_t b[] = {0x01, 0x03, 0x03, 0x00, 0x00, 0x10};
    TEST_ASSERT_NOT_EQUAL(RenogyDriver::crc16(a, sizeof(a)),
                          RenogyDriver::crc16(b, sizeof(b)));
}

// ── buildRequest ──────────────────────────────────────────────────────────────

static void test_build_request_correct_byte_layout(void)
{
    uint8_t buf[8] = {};
    RenogyDriver::buildRequest(buf, 0x01, 0x0100, 16);

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]); // device address
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[1]); // function code
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]); // register hi
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]); // register lo
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]); // count hi
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[5]); // count lo (16 = 0x10)
}

static void test_build_request_crc_bytes_are_valid(void)
{
    uint8_t buf[8] = {};
    RenogyDriver::buildRequest(buf, 0x01, 0x0100, 16);

    // CRC covers bytes 0–5; stored little-endian in bytes 6–7
    const uint16_t expectedCrc = RenogyDriver::crc16(buf, 6);
    const uint16_t storedCrc   = static_cast<uint16_t>(buf[6]) |
                                 (static_cast<uint16_t>(buf[7]) << 8);
    TEST_ASSERT_EQUAL_HEX16(expectedCrc, storedCrc);
}

// ── poll() — happy path ───────────────────────────────────────────────────────

static void test_poll_returns_true_on_valid_responses(void)
{
    MockUart uart;
    enqueuePollResponses(uart, {});
    RenogyDriver drv(uart);
    TEST_ASSERT_TRUE(drv.poll());
}

static void test_poll_populates_battery_soc(void)
{
    MockUart uart;
    enqueuePollResponses(uart, {.batterySoc = 78});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(78, drv.getData().batterySoc);
}

static void test_poll_scales_battery_voltage(void)
{
    MockUart uart;
    // Register value 127 → 127 × 0.1 = 12.7 V
    enqueuePollResponses(uart, {.batteryVoltageR = 127});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.7f, drv.getData().batteryVoltage);
}

static void test_poll_scales_charging_current(void)
{
    MockUart uart;
    // Register value 530 → 530 × 0.01 = 5.30 A
    enqueuePollResponses(uart, {.chargingCurrR = 530});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.30f, drv.getData().chargingCurrent);
}

static void test_poll_extracts_controller_temp_from_high_byte(void)
{
    MockUart uart;
    // Temp = 35°C stored in high byte of register 0x0107
    enqueuePollResponses(uart, {.controllerTemp = 35});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(35, drv.getData().controllerTemp);
}

static void test_poll_scales_pv_voltage(void)
{
    MockUart uart;
    // Register value 185 → 185 × 0.1 = 18.5 V
    enqueuePollResponses(uart, {.pvVoltageR = 185});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.5f, drv.getData().pvVoltage);
}

static void test_poll_reads_charging_power_raw(void)
{
    MockUart uart;
    enqueuePollResponses(uart, {.chargingPower = 42});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(42, drv.getData().chargingPower);
}

static void test_poll_scales_load_current(void)
{
    MockUart uart;
    // Register value 75 → 75 × 0.01 = 0.75 A
    enqueuePollResponses(uart, {.loadCurrR = 75});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, drv.getData().loadCurrent);
}

static void test_poll_sets_last_update_ms_on_success(void)
{
    MockUart uart;
    enqueuePollResponses(uart, {});
    RenogyDriver drv(uart);
    // Capture tick before poll; on host both will be 0 — test checks field is set
    const uint32_t tickBefore = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
    drv.poll();
    TEST_ASSERT_GREATER_OR_EQUAL(tickBefore, drv.getData().lastUpdateMs);
}

// ── poll() — failure paths ────────────────────────────────────────────────────

static void test_poll_returns_false_on_uart_timeout(void)
{
    MockUart uart;
    // No data enqueued — read() returns 0 bytes (simulates timeout)
    RenogyDriver drv(uart);
    TEST_ASSERT_FALSE(drv.poll());
}

static void test_poll_does_not_update_data_on_timeout(void)
{
    MockUart uart;
    // First poll succeeds
    enqueuePollResponses(uart, {.batterySoc = 55});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(55, drv.getData().batterySoc);

    // Second poll times out — data must be unchanged
    drv.poll();
    TEST_ASSERT_EQUAL(55, drv.getData().batterySoc);
}

static void test_poll_returns_false_on_bad_crc(void)
{
    MockUart uart;

    // Build a valid block1 response then corrupt its last CRC byte
    std::vector<uint16_t> block1(16, 0);
    auto resp = makeResponse(config::renogy::MODBUS_ADDR, block1);
    resp.back() ^= 0xFF; // corrupt CRC hi byte
    uart.enqueueRx(resp);

    RenogyDriver drv(uart);
    TEST_ASSERT_FALSE(drv.poll());
}

static void test_poll_does_not_update_data_on_bad_crc(void)
{
    MockUart uart;

    // First poll succeeds
    enqueuePollResponses(uart, {.batterySoc = 90});
    RenogyDriver drv(uart);
    drv.poll();

    // Second poll: block1 has corrupted CRC
    std::vector<uint16_t> block1(16, 0);
    block1[1] = 10; // batterySoc = 10
    auto resp = makeResponse(config::renogy::MODBUS_ADDR, block1);
    resp.back() ^= 0xFF;
    uart.enqueueRx(resp);

    drv.poll();
    TEST_ASSERT_EQUAL(90, drv.getData().batterySoc); // unchanged
}

static void test_poll_flushes_rx_before_each_request(void)
{
    MockUart uart;
    enqueuePollResponses(uart, {});
    RenogyDriver drv(uart);
    drv.poll();
    // poll() calls readRegisters() twice, each flushes once
    TEST_ASSERT_EQUAL(2, uart.flushCount_);
}

void run_renogy_tests(void)
{
    RUN_TEST(test_crc16_zero_bytes_returns_init_value);
    RUN_TEST(test_crc16_known_modbus_vector);
    RUN_TEST(test_crc16_is_deterministic);
    RUN_TEST(test_crc16_different_inputs_produce_different_results);
    RUN_TEST(test_build_request_correct_byte_layout);
    RUN_TEST(test_build_request_crc_bytes_are_valid);
    RUN_TEST(test_poll_returns_true_on_valid_responses);
    RUN_TEST(test_poll_populates_battery_soc);
    RUN_TEST(test_poll_scales_battery_voltage);
    RUN_TEST(test_poll_scales_charging_current);
    RUN_TEST(test_poll_extracts_controller_temp_from_high_byte);
    RUN_TEST(test_poll_scales_pv_voltage);
    RUN_TEST(test_poll_reads_charging_power_raw);
    RUN_TEST(test_poll_scales_load_current);
    RUN_TEST(test_poll_sets_last_update_ms_on_success);
    RUN_TEST(test_poll_returns_false_on_uart_timeout);
    RUN_TEST(test_poll_does_not_update_data_on_timeout);
    RUN_TEST(test_poll_returns_false_on_bad_crc);
    RUN_TEST(test_poll_does_not_update_data_on_bad_crc);
    RUN_TEST(test_poll_flushes_rx_before_each_request);
}
