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

// Build a single poll() response: block 1 (0x0100-0x0109).
struct PollValues {
    uint16_t batterySocR      = 0;  // raw = %
    uint16_t batteryVoltageR  = 0;  // raw × 0.1 = V
    uint16_t batteryCurrentR  = 0;  // raw signed × 0.01 = A
    uint16_t controllerTempHi = 0;  // placed in high byte of reg 0x0103
    uint16_t loadPower        = 0;  // raw W
    uint16_t pvVoltageR       = 0;  // raw × 0.1 = V
    uint16_t pvCurrentR       = 0;  // raw × 0.01 = A
    uint16_t pvPower          = 0;  // raw W
};

// Historical data for poll() block 2 (0x010B-0x0114) and block 3 (0x0120).
struct HistoricalValues {
    uint16_t maxChargingPowerW = 0;  // raw = W   (index 4 = reg 0x010F)
    uint16_t dailyGenWh        = 0;  // raw = Wh  (index 8 = reg 0x0113)
    uint16_t dailyConWh        = 0;  // raw = Wh  (index 9 = reg 0x0114)
    uint8_t  chargingStatus    = 0;  // 0–6       (reg 0x0120 low byte)
};

// poll() issues three readRegisters calls; enqueue all three responses.
static void enqueuePollResponse(MockUart& uart, const PollValues& v,
                                const HistoricalValues& h = {})
{
    // Block 1: 0x0100–0x0109
    std::vector<uint16_t> regs(10, 0);
    regs[0] = v.batterySocR;
    regs[1] = v.batteryVoltageR;
    regs[2] = v.batteryCurrentR;
    regs[3] = static_cast<uint16_t>(v.controllerTempHi) << 8;
    regs[6] = v.loadPower;
    regs[7] = v.pvVoltageR;
    regs[8] = v.pvCurrentR;
    regs[9] = v.pvPower;
    uart.enqueueRx(makeResponse(config::renogy::MODBUS_ADDR, regs));

    // Block 2: 0x010B–0x0114
    std::vector<uint16_t> hist(10, 0);
    hist[4] = h.maxChargingPowerW;
    hist[8] = h.dailyGenWh;
    hist[9] = h.dailyConWh;
    uart.enqueueRx(makeResponse(config::renogy::MODBUS_ADDR, hist));

    // Block 3: 0x0120 (1 register — low byte = charging status)
    std::vector<uint16_t> status(1, static_cast<uint16_t>(h.chargingStatus));
    uart.enqueueRx(makeResponse(config::renogy::MODBUS_ADDR, status));
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
    //   Frame:   01 03 00 00 00 01  →  CRC = 0x0A84
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX16(0x0A84, RenogyDriver::crc16(frame, sizeof(frame)));
}

static void test_crc16_is_deterministic(void)
{
    const uint8_t data[] = {0x01, 0x03, 0x01, 0x00, 0x00, 0x0B};
    TEST_ASSERT_EQUAL_HEX16(RenogyDriver::crc16(data, sizeof(data)),
                             RenogyDriver::crc16(data, sizeof(data)));
}

static void test_crc16_different_inputs_produce_different_results(void)
{
    const uint8_t a[] = {0x01, 0x03, 0x01, 0x00, 0x00, 0x0B};
    const uint8_t b[] = {0x01, 0x03, 0x03, 0x00, 0x00, 0x0B};
    TEST_ASSERT_NOT_EQUAL(RenogyDriver::crc16(a, sizeof(a)),
                          RenogyDriver::crc16(b, sizeof(b)));
}

// ── buildRequest ──────────────────────────────────────────────────────────────

static void test_build_request_correct_byte_layout(void)
{
    uint8_t buf[8] = {};
    RenogyDriver::buildRequest(buf, 0x01, 0x0100, 10);

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]); // device address
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[1]); // function code
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]); // register hi
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]); // register lo
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]); // count hi
    TEST_ASSERT_EQUAL_HEX8(0x0A, buf[5]); // count lo (10 = 0x0A)
}

static void test_build_request_crc_bytes_are_valid(void)
{
    uint8_t buf[8] = {};
    RenogyDriver::buildRequest(buf, 0x01, 0x0100, 10);

    const uint16_t expectedCrc = RenogyDriver::crc16(buf, 6);
    const uint16_t storedCrc   = static_cast<uint16_t>(buf[6]) |
                                 (static_cast<uint16_t>(buf[7]) << 8);
    TEST_ASSERT_EQUAL_HEX16(expectedCrc, storedCrc);
}

// ── poll() — happy path ───────────────────────────────────────────────────────

static void test_poll_returns_true_on_valid_response(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {});
    RenogyDriver drv(uart);
    TEST_ASSERT_TRUE(drv.poll());
}

static void test_poll_populates_battery_soc(void)
{
    MockUart uart;
    // Raw register value = SOC % directly (0–100)
    enqueuePollResponse(uart, {.batterySocR = 78});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(78, drv.getData().batterySoc);
}

static void test_poll_scales_battery_voltage(void)
{
    MockUart uart;
    // Raw 127 → 127 × 0.1 = 12.7 V
    enqueuePollResponse(uart, {.batteryVoltageR = 127});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.7f, drv.getData().batteryVoltage);
}

static void test_poll_scales_battery_current_positive(void)
{
    MockUart uart;
    // Raw 530 → 530 × 0.01 = 5.30 A (charging)
    enqueuePollResponse(uart, {.batteryCurrentR = 530});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.30f, drv.getData().batteryCurrent);
}

static void test_poll_scales_battery_current_negative(void)
{
    MockUart uart;
    // Raw -200 as signed int16 (0xFF38) → -200 × 0.01 = -2.00 A (discharging)
    enqueuePollResponse(uart, {.batteryCurrentR = static_cast<uint16_t>(-200)});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.0f, drv.getData().batteryCurrent);
}

static void test_poll_decodes_controller_temp_positive(void)
{
    MockUart uart;
    // High byte = 0x23 (35°C, sign bit clear) → 35.0°C
    enqueuePollResponse(uart, {.controllerTempHi = 0x23});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 35.0f, drv.getData().controllerTemp);
}

static void test_poll_decodes_controller_temp_negative(void)
{
    MockUart uart;
    // High byte = 0x8A (sign bit set, magnitude 10) → -10°C
    enqueuePollResponse(uart, {.controllerTempHi = 0x8A});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, drv.getData().controllerTemp);
}

static void test_poll_scales_pv_voltage(void)
{
    MockUart uart;
    // Raw 185 → 185 × 0.1 = 18.5 V
    enqueuePollResponse(uart, {.pvVoltageR = 185});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.5f, drv.getData().pvVoltage);
}

static void test_poll_reads_pv_power_raw(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {.pvPower = 42});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(42, drv.getData().pvPower);
}

static void test_poll_reads_load_power_raw(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {.loadPower = 15});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(15, drv.getData().loadPower);
}

static void test_poll_sets_last_update_ms_on_success(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {});
    RenogyDriver drv(uart);
    const uint32_t tickBefore = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
    drv.poll();
    TEST_ASSERT_GREATER_OR_EQUAL(tickBefore, drv.getData().lastUpdateMs);
}

// ── poll() — failure paths ────────────────────────────────────────────────────

static void test_poll_returns_false_on_uart_timeout(void)
{
    MockUart uart;
    RenogyDriver drv(uart);
    TEST_ASSERT_FALSE(drv.poll());
}

static void test_poll_does_not_update_data_on_timeout(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {.batterySocR = 55}); // 55 %
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(55, drv.getData().batterySoc);

    drv.poll(); // no data enqueued — timeout
    TEST_ASSERT_EQUAL(55, drv.getData().batterySoc); // unchanged
}

static void test_poll_returns_false_on_bad_crc(void)
{
    MockUart uart;
    std::vector<uint16_t> regs(10, 0);
    auto resp = makeResponse(config::renogy::MODBUS_ADDR, regs);
    resp.back() ^= 0xFF; // corrupt CRC hi byte
    uart.enqueueRx(resp);

    RenogyDriver drv(uart);
    TEST_ASSERT_FALSE(drv.poll());
}

static void test_poll_does_not_update_data_on_bad_crc(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {.batterySocR = 90}); // 90 %
    RenogyDriver drv(uart);
    drv.poll();

    std::vector<uint16_t> regs(10, 0);
    regs[0] = 10; // would be 10 % if parsed
    auto resp = makeResponse(config::renogy::MODBUS_ADDR, regs);
    resp.back() ^= 0xFF;
    uart.enqueueRx(resp);

    drv.poll();
    TEST_ASSERT_EQUAL(90, drv.getData().batterySoc); // unchanged
}

static void test_poll_flushes_rx_before_request(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {});
    RenogyDriver drv(uart);
    drv.poll();
    // poll() issues 3 readRegisters calls (blocks 0x0100, 0x010B, 0x0120),
    // each of which flushes before sending its request.
    TEST_ASSERT_EQUAL(3, uart.flushCount_);
}

// ── poll() — historical fields ────────────────────────────────────────────────

static void test_poll_reads_max_charging_power_today(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {}, {.maxChargingPowerW = 85});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(85, drv.getData().maxChargingPowerToday);
}

static void test_poll_reads_daily_generation_wh(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {}, {.dailyGenWh = 350});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(350, drv.getData().dailyGenerationWh);
}

static void test_poll_reads_daily_consumption_wh(void)
{
    MockUart uart;
    enqueuePollResponse(uart, {}, {.dailyConWh = 120});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(120, drv.getData().dailyConsumptionWh);
}

static void test_poll_decodes_charging_status(void)
{
    MockUart uart;
    // 2 = MPPT charging mode
    enqueuePollResponse(uart, {}, {.chargingStatus = 2});
    RenogyDriver drv(uart);
    drv.poll();
    TEST_ASSERT_EQUAL(2, drv.getData().chargingStatus);
}

// ── buildWriteRequest ─────────────────────────────────────────────────────────

static void test_build_write_request_correct_byte_layout(void)
{
    uint8_t buf[8] = {};
    RenogyDriver::buildWriteRequest(buf, 0x01, 0x010A, 1);

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]); // device address
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[1]); // function code
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]); // register hi
    TEST_ASSERT_EQUAL_HEX8(0x0A, buf[3]); // register lo
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]); // value hi
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[5]); // value lo
}

static void test_build_write_request_crc_is_valid(void)
{
    uint8_t buf[8] = {};
    RenogyDriver::buildWriteRequest(buf, 0x01, 0x010A, 1);

    const uint16_t expectedCrc = RenogyDriver::crc16(buf, 6);
    const uint16_t storedCrc   = static_cast<uint16_t>(buf[6]) |
                                 (static_cast<uint16_t>(buf[7]) << 8);
    TEST_ASSERT_EQUAL_HEX16(expectedCrc, storedCrc);
}

// ── setLoad() ─────────────────────────────────────────────────────────────────

// Enqueue a valid echo response for a write-single-register command.
static void enqueueWriteEcho(MockUart& uart, uint8_t addr,
                              uint16_t reg, uint16_t value)
{
    uint8_t frame[8];
    RenogyDriver::buildWriteRequest(frame, addr, reg, value);
    uart.enqueueRx(std::vector<uint8_t>(frame, frame + sizeof(frame)));
}

static void test_set_load_on_returns_true_on_valid_echo(void)
{
    MockUart uart;
    // setLoad() first sets manual mode (0xE01D=0x0F), then sends the on/off command.
    enqueueWriteEcho(uart, config::renogy::MODBUS_ADDR, 0xE01D, 0x000F);
    enqueueWriteEcho(uart, config::renogy::MODBUS_ADDR, 0x010A, 1);
    RenogyDriver drv(uart);
    TEST_ASSERT_TRUE(drv.setLoad(true));
}

static void test_set_load_off_returns_true_on_valid_echo(void)
{
    MockUart uart;
    enqueueWriteEcho(uart, config::renogy::MODBUS_ADDR, 0xE01D, 0x000F);
    enqueueWriteEcho(uart, config::renogy::MODBUS_ADDR, 0x010A, 0);
    RenogyDriver drv(uart);
    TEST_ASSERT_TRUE(drv.setLoad(false));
}

static void test_set_load_returns_false_on_timeout(void)
{
    MockUart uart; // no data enqueued — mode write times out
    RenogyDriver drv(uart);
    TEST_ASSERT_FALSE(drv.setLoad(true));
}

static void test_set_load_returns_false_on_bad_crc(void)
{
    MockUart uart;
    // Corrupt CRC on the mode register echo — load write is never attempted.
    uint8_t frame[8];
    RenogyDriver::buildWriteRequest(frame, config::renogy::MODBUS_ADDR, 0xE01D, 0x000F);
    frame[7] ^= 0xFF;
    uart.enqueueRx(std::vector<uint8_t>(frame, frame + sizeof(frame)));
    RenogyDriver drv(uart);
    TEST_ASSERT_FALSE(drv.setLoad(true));
}

static void test_set_load_returns_false_on_mismatched_echo(void)
{
    MockUart uart;
    // Mode write succeeds; load echo reports value=0 but we sent value=1.
    enqueueWriteEcho(uart, config::renogy::MODBUS_ADDR, 0xE01D, 0x000F);
    enqueueWriteEcho(uart, config::renogy::MODBUS_ADDR, 0x010A, 0);
    RenogyDriver drv(uart);
    TEST_ASSERT_FALSE(drv.setLoad(true));
}

void run_renogy_tests(void)
{
    RUN_TEST(test_crc16_zero_bytes_returns_init_value);
    RUN_TEST(test_crc16_known_modbus_vector);
    RUN_TEST(test_crc16_is_deterministic);
    RUN_TEST(test_crc16_different_inputs_produce_different_results);
    RUN_TEST(test_build_request_correct_byte_layout);
    RUN_TEST(test_build_request_crc_bytes_are_valid);
    RUN_TEST(test_poll_returns_true_on_valid_response);
    RUN_TEST(test_poll_populates_battery_soc);
    RUN_TEST(test_poll_scales_battery_voltage);
    RUN_TEST(test_poll_scales_battery_current_positive);
    RUN_TEST(test_poll_scales_battery_current_negative);
    RUN_TEST(test_poll_decodes_controller_temp_positive);
    RUN_TEST(test_poll_decodes_controller_temp_negative);
    RUN_TEST(test_poll_scales_pv_voltage);
    RUN_TEST(test_poll_reads_pv_power_raw);
    RUN_TEST(test_poll_reads_load_power_raw);
    RUN_TEST(test_poll_sets_last_update_ms_on_success);
    RUN_TEST(test_poll_returns_false_on_uart_timeout);
    RUN_TEST(test_poll_does_not_update_data_on_timeout);
    RUN_TEST(test_poll_returns_false_on_bad_crc);
    RUN_TEST(test_poll_does_not_update_data_on_bad_crc);
    RUN_TEST(test_poll_flushes_rx_before_request);
    RUN_TEST(test_poll_reads_max_charging_power_today);
    RUN_TEST(test_poll_reads_daily_generation_wh);
    RUN_TEST(test_poll_reads_daily_consumption_wh);
    RUN_TEST(test_poll_decodes_charging_status);
    RUN_TEST(test_build_write_request_correct_byte_layout);
    RUN_TEST(test_build_write_request_crc_is_valid);
    RUN_TEST(test_set_load_on_returns_true_on_valid_echo);
    RUN_TEST(test_set_load_off_returns_true_on_valid_echo);
    RUN_TEST(test_set_load_returns_false_on_timeout);
    RUN_TEST(test_set_load_returns_false_on_bad_crc);
    RUN_TEST(test_set_load_returns_false_on_mismatched_echo);
}
