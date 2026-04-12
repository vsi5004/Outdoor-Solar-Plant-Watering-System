#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/esp_uart.hpp"
#include "drivers/renogy_driver.hpp"
#include "config.hpp"

static const char* TAG = "test_renogy";

// ---------------------------------------------------------------------------
// HIL test — Renogy Wanderer Modbus RTU (UART1, GPIO4/5, 9600 8N1)
//
// Sends one poll request and logs the raw telemetry. Check values against
// what the Renogy controller display shows to confirm the driver is parsing
// registers correctly.
// ---------------------------------------------------------------------------

void register_renogy_tests() {}

TEST_CASE("renogy: poll once and log telemetry", "[renogy]")
{
    // Clean up any driver left over from a previous run (e.g. after a failed
    // test that exited early via TEST_FAIL_MESSAGE / longjmp).
    uart_driver_delete(static_cast<uart_port_t>(config::pins::RENOGY_UART));

    EspUart uart(static_cast<uart_port_t>(config::pins::RENOGY_UART),
                 config::pins::RENOGY_TX,
                 config::pins::RENOGY_RX,
                 config::renogy::BAUD_RATE);

    RenogyDriver driver(uart);

    const bool ok = driver.poll();

    if (!ok) {
        ESP_LOGE(TAG, "poll() failed — check wiring and RS232 adapter");
        TEST_FAIL_MESSAGE("Renogy poll timed out or returned a CRC error");
        return;
    }

    const RenogyData d = driver.getData();

    ESP_LOGI(TAG, "Battery SOC:       %u %%",      d.batterySoc);
    ESP_LOGI(TAG, "Battery voltage:   %.2f V",     d.batteryVoltage);
    ESP_LOGI(TAG, "Battery current:   %.2f A",     d.batteryCurrent);
    ESP_LOGI(TAG, "PV voltage:        %.2f V",     d.pvVoltage);
    ESP_LOGI(TAG, "PV current:        %.2f A",     d.pvCurrent);
    ESP_LOGI(TAG, "PV power:          %u W",       d.pvPower);
    ESP_LOGI(TAG, "Load power:        %u W",       d.loadPower);
    ESP_LOGI(TAG, "Controller temp:   %.1f C",     d.controllerTemp);

    uart_driver_delete(static_cast<uart_port_t>(config::pins::RENOGY_UART));
}

TEST_CASE("renogy: toggle load output on then off", "[renogy]")
{
    uart_driver_delete(static_cast<uart_port_t>(config::pins::RENOGY_UART));

    EspUart uart(static_cast<uart_port_t>(config::pins::RENOGY_UART),
                 config::pins::RENOGY_TX,
                 config::pins::RENOGY_RX,
                 config::renogy::BAUD_RATE);

    RenogyDriver driver(uart);

    ESP_LOGI(TAG, "Turning load ON...");
    const bool onOk = driver.setLoad(true);
    TEST_ASSERT_TRUE_MESSAGE(onOk, "setLoad(true) failed — check wiring and load mode setting");

    ESP_LOGI(TAG, "Load ON — waiting 1 s...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Turning load OFF...");
    const bool offOk = driver.setLoad(false);
    TEST_ASSERT_TRUE_MESSAGE(offOk, "setLoad(false) failed");

    ESP_LOGI(TAG, "Load OFF");

    uart_driver_delete(static_cast<uart_port_t>(config::pins::RENOGY_UART));
}
