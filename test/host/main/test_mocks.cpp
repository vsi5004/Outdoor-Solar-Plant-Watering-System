#include "unity.h"
#include "mock_gpio.hpp"
#include "mock_pwm.hpp"
#include "mock_adc_channel.hpp"
#include "mock_uart.hpp"

// ── MockGpio ─────────────────────────────────────────────────────────────────

static void test_gpio_starts_low(void)
{
    MockGpio gpio;
    TEST_ASSERT_FALSE(gpio.getLevel());
    TEST_ASSERT_EQUAL(0, gpio.setHighCount_);
    TEST_ASSERT_EQUAL(0, gpio.setLowCount_);
}

static void test_gpio_tracks_level_and_counts(void)
{
    MockGpio gpio;
    gpio.setHigh();
    TEST_ASSERT_TRUE(gpio.getLevel());
    TEST_ASSERT_EQUAL(1, gpio.setHighCount_);

    gpio.setLow();
    TEST_ASSERT_FALSE(gpio.getLevel());
    TEST_ASSERT_EQUAL(1, gpio.setLowCount_);
}

static void test_gpio_accumulates_counts(void)
{
    MockGpio gpio;
    gpio.setHigh();
    gpio.setHigh();
    gpio.setLow();
    TEST_ASSERT_EQUAL(2, gpio.setHighCount_);
    TEST_ASSERT_EQUAL(1, gpio.setLowCount_);
}

// ── MockPwm ──────────────────────────────────────────────────────────────────

static void test_pwm_starts_at_zero(void)
{
    MockPwm pwm;
    TEST_ASSERT_EQUAL(0,   pwm.lastDuty_);
    TEST_ASSERT_EQUAL(0,   pwm.setDutyCallCount_);
    TEST_ASSERT_TRUE(pwm.callLog_.empty());
}

static void test_pwm_records_set_duty(void)
{
    MockPwm pwm;
    pwm.setDutyPercent(75);
    TEST_ASSERT_EQUAL(75, pwm.lastDuty_);
    TEST_ASSERT_EQUAL(1,  pwm.setDutyCallCount_);
    TEST_ASSERT_EQUAL(1u, pwm.callLog_.size());
    TEST_ASSERT_EQUAL(MockPwm::Call::Kind::SetDuty, pwm.callLog_[0].kind);
    TEST_ASSERT_EQUAL(75, pwm.callLog_[0].duty);
}

static void test_pwm_stop_zeroes_duty_and_logs(void)
{
    MockPwm pwm;
    pwm.setDutyPercent(50);
    pwm.stop();
    TEST_ASSERT_EQUAL(0, pwm.lastDuty_);
    TEST_ASSERT_EQUAL(1, pwm.stopCallCount_);
    TEST_ASSERT_EQUAL(1, pwm.setDutyCallCount_); // stop() does not increment this
    TEST_ASSERT_EQUAL(MockPwm::Call::Kind::Stop, pwm.callLog_[1].kind);
}

static void test_pwm_reset_clears_state(void)
{
    MockPwm pwm;
    pwm.setDutyPercent(80);
    pwm.reset();
    TEST_ASSERT_EQUAL(0,  pwm.lastDuty_);
    TEST_ASSERT_EQUAL(0,  pwm.setDutyCallCount_);
    TEST_ASSERT_TRUE(pwm.callLog_.empty());
}

// ── MockAdcChannel ───────────────────────────────────────────────────────────

static void test_adc_returns_configured_value(void)
{
    MockAdcChannel adc(1500.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1500.0f, adc.readMillivolts());
}

static void test_adc_set_return_value_changes_reads(void)
{
    MockAdcChannel adc;
    adc.setReturnValue(800.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 800.0f, adc.readMillivolts());
    adc.setReturnValue(2400.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2400.0f, adc.readMillivolts());
}

static void test_adc_tracks_read_count(void)
{
    MockAdcChannel adc;
    adc.readMillivolts();
    adc.readMillivolts();
    TEST_ASSERT_EQUAL(2, adc.readCount_);
}

// ── MockUart ─────────────────────────────────────────────────────────────────

static void test_uart_write_records_tx_bytes(void)
{
    MockUart uart;
    const uint8_t tx[] = {0x01, 0x03, 0x01, 0x00};
    uart.write(tx, sizeof(tx));
    TEST_ASSERT_EQUAL(1, uart.writeCallCount_);
    TEST_ASSERT_EQUAL(sizeof(tx), uart.lastTx_.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, uart.lastTx_.data(), sizeof(tx));
}

static void test_uart_enqueue_then_read(void)
{
    MockUart uart;
    uart.enqueueRx({0xAA, 0xBB, 0xCC});
    uint8_t buf[3] = {};
    TEST_ASSERT_EQUAL(3u, uart.read(buf, 3, 100));
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, buf[2]);
}

static void test_uart_read_returns_available_bytes_only(void)
{
    MockUart uart;
    uart.enqueueRx({0x11, 0x22});
    uint8_t buf[8] = {};
    TEST_ASSERT_EQUAL(2u, uart.read(buf, sizeof(buf), 100));
}

static void test_uart_read_returns_zero_on_empty_buffer(void)
{
    MockUart uart;
    uint8_t buf[4] = {};
    TEST_ASSERT_EQUAL(0u, uart.read(buf, sizeof(buf), 50));
}

static void test_uart_flush_discards_queued_bytes(void)
{
    MockUart uart;
    uart.discardOnFlush = true;
    uart.enqueueRx({0x01, 0x02, 0x03});
    uart.flushRx();
    uint8_t buf[4] = {};
    TEST_ASSERT_EQUAL(0u, uart.read(buf, sizeof(buf), 50));
    TEST_ASSERT_EQUAL(1, uart.flushCount_);
}

static void test_uart_consecutive_reads_consume_in_order(void)
{
    MockUart uart;
    uart.enqueueRx({0x01, 0x02, 0x03, 0x04});
    uint8_t a[2] = {}, b[2] = {};
    uart.read(a, 2, 50);
    uart.read(b, 2, 50);
    TEST_ASSERT_EQUAL_HEX8(0x01, a[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, a[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, b[1]);
}

void run_mock_tests(void)
{
    RUN_TEST(test_gpio_starts_low);
    RUN_TEST(test_gpio_tracks_level_and_counts);
    RUN_TEST(test_gpio_accumulates_counts);
    RUN_TEST(test_pwm_starts_at_zero);
    RUN_TEST(test_pwm_records_set_duty);
    RUN_TEST(test_pwm_stop_zeroes_duty_and_logs);
    RUN_TEST(test_pwm_reset_clears_state);
    RUN_TEST(test_adc_returns_configured_value);
    RUN_TEST(test_adc_set_return_value_changes_reads);
    RUN_TEST(test_adc_tracks_read_count);
    RUN_TEST(test_uart_write_records_tx_bytes);
    RUN_TEST(test_uart_enqueue_then_read);
    RUN_TEST(test_uart_read_returns_available_bytes_only);
    RUN_TEST(test_uart_read_returns_zero_on_empty_buffer);
    RUN_TEST(test_uart_flush_discards_queued_bytes);
    RUN_TEST(test_uart_consecutive_reads_consume_in_order);
}
