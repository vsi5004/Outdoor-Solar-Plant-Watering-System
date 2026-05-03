#include <cstdint>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_zigbee_core.h"

// HAL
#include "hal/status_led.hpp"
#include "hal/ledc_pwm.hpp"
#include "hal/esp_adc_channel.hpp"
#include "hal/esp_uart.hpp"
#include "hal/esp_pcnt.hpp"
#include "hal/esp_gpio.hpp"

// Drivers
#include "drivers/solenoid_actuator.hpp"
#include "drivers/pump_actuator.hpp"
#include "drivers/flow_meter.hpp"
#include "drivers/float_sensor.hpp"
#include "drivers/renogy_driver.hpp"

// Watering
#include "watering/zone_manager.hpp"
#include "watering/watering_fsm.hpp"
#include "watering/water_usage_tracker.hpp"

// Zigbee
#include "zb/zb_device.hpp"
#include "zb/zb_handlers.hpp"

#include "config.hpp"

static const char *TAG = "main";

// ── Forward declarations ──────────────────────────────────────────────────────

static void wateringTask(void *arg);
static void renogyTask(void *arg);
static void zbTask(void *arg);

static void startNetworkSteering(uint8_t modeMask)
{
    const esp_err_t err = esp_zb_bdb_start_top_level_commissioning(modeMask);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to start network steering: %s", esp_err_to_name(err));
    }
}

static const char *zoneStatusName(ZoneStatus status)
{
    switch (status)
    {
    case ZoneStatus::Idle:
        return "idle";
    case ZoneStatus::Priming:
        return "priming";
    case ZoneStatus::Running:
        return "running";
    case ZoneStatus::Fault:
        return "fault";
    default:
        return "unknown";
    }
}

static const char *faultCodeName(FaultCode code)
{
    switch (code)
    {
    case FaultCode::None:
        return "none";
    case FaultCode::LowBattery:
        return "battery_low";
    case FaultCode::LowWater:
        return "water_low";
    case FaultCode::PrimeTimeout:
        return "prime_timeout";
    case FaultCode::MaxDuration:
        return "max_duration";
    case FaultCode::InvalidRequest:
        return "invalid_request";
    case FaultCode::LoadEnableFailed:
        return "load_enable_failed";
    case FaultCode::StaleData:
        return "stale_data";
    default:
        return "unknown";
    }
}

enum class WatererStateCode : uint8_t
{
    Idle = 0,
    Priming = 1,
    Watering = 2,
    Fault = 3,
};

static const char *watererStateName(WatererStateCode state)
{
    switch (state)
    {
    case WatererStateCode::Idle:
        return "idle";
    case WatererStateCode::Priming:
        return "priming";
    case WatererStateCode::Watering:
        return "watering";
    case WatererStateCode::Fault:
        return "fault";
    default:
        return "unknown";
    }
}

static WatererStateCode watererStateFromStatuses(const ZoneStatus (&statuses)[ZONE_COUNT],
                                                 FaultCode fault)
{
    if (fault != FaultCode::None)
    {
        return WatererStateCode::Fault;
    }

    bool anyWatering = false;
    for (ZoneStatus status : statuses)
    {
        if (status == ZoneStatus::Priming)
        {
            return WatererStateCode::Priming;
        }
        if (status == ZoneStatus::Running)
        {
            anyWatering = true;
        }
    }

    return anyWatering ? WatererStateCode::Watering : WatererStateCode::Idle;
}

static uint8_t activeZoneFromStatuses(const ZoneStatus (&statuses)[ZONE_COUNT])
{
    for (uint8_t i = 0; i < ZONE_COUNT; ++i)
    {
        if (statuses[i] == ZoneStatus::Priming ||
            statuses[i] == ZoneStatus::Running ||
            statuses[i] == ZoneStatus::Fault)
        {
            return static_cast<uint8_t>(i + 1u);
        }
    }
    return 0u;
}

static const char *waterUsageNvsKey(ZoneId zone)
{
    switch (zone)
    {
    case ZoneId::Zone1:
        return "z1_ml";
    case ZoneId::Zone2:
        return "z2_ml";
    case ZoneId::Zone3:
        return "z3_ml";
    case ZoneId::Zone4:
        return "z4_ml";
    case ZoneId::Zone5:
        return "z5_ml";
    default:
        return "unknown";
    }
}

// ── Object graph (static storage — survives after app_main returns) ───────────

// Queue for Zigbee → watering-task command handoff (depth 4, ZbWateringCmd items).
static QueueHandle_t s_cmdQueue;
static std::atomic_bool s_zigbeeJoined{false};

// GPIO
static EspGpio s_drvMasterEn(config::pins::DRV_MASTER_EN);

// LEDC PWM channels
static LedcPwm s_pumpPwm(config::pins::PUMP_RPWM, LEDC_CHANNEL_0);
static LedcPwm s_sol5Pwm(config::pins::SOL5_RPWM, LEDC_CHANNEL_1);
static LedcPwm s_sol1Pwm(config::pins::SOL1_LPWM, LEDC_CHANNEL_2);
static LedcPwm s_sol2Pwm(config::pins::SOL2_RPWM, LEDC_CHANNEL_3);
static LedcPwm s_sol3Pwm(config::pins::SOL3_LPWM, LEDC_CHANNEL_4);
static LedcPwm s_sol4Pwm(config::pins::SOL4_RPWM, LEDC_CHANNEL_5);

// Solenoids (Zone1–Zone5)
static SolenoidActuator s_sol1(s_sol1Pwm);
static SolenoidActuator s_sol2(s_sol2Pwm);
static SolenoidActuator s_sol3(s_sol3Pwm);
static SolenoidActuator s_sol4(s_sol4Pwm);
static SolenoidActuator s_sol5(s_sol5Pwm);

// ADC channels — created after the unit is initialised in app_main.
static EspAdcChannel *s_floatAdcCh = nullptr; // ADC1_CH0 — float sensor

static PumpActuator *s_pump = nullptr;
static FloatSensor *s_tank = nullptr;

// Flow meter
static EspPcnt s_pcnt(config::pins::FLOW_METER);
static FlowMeter s_flow(s_pcnt);

// Renogy UART + driver
static EspUart s_uart(static_cast<uart_port_t>(config::pins::RENOGY_UART),
                      config::pins::RENOGY_TX,
                      config::pins::RENOGY_RX,
                      config::renogy::BAUD_RATE);
static RenogyDriver s_renogy(s_uart);

// Zone manager and FSM (built after ADC objects are ready)
static ZoneManager *s_zones = nullptr;
static WateringFsm *s_fsm = nullptr;
static WaterUsageTracker s_waterUsage;

static void loadWaterUsageTotals()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("water_usage", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No persisted water usage totals found");
        return;
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open water usage NVS namespace: %s", esp_err_to_name(err));
        return;
    }

    for (uint8_t i = 0; i < ZONE_COUNT; ++i)
    {
        const auto zone = static_cast<ZoneId>(i + 1u);
        uint64_t totalMl = 0u;
        err = nvs_get_u64(handle, waterUsageNvsKey(zone), &totalMl);
        if (err == ESP_OK)
        {
            s_waterUsage.setTotalMl(zone, totalMl);
            ESP_LOGI(TAG, "Restored Zone %u water total: %llu mL",
                     static_cast<unsigned>(zone),
                     static_cast<unsigned long long>(totalMl));
        }
        else if (err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG, "Failed to read Zone %u water total: %s",
                     static_cast<unsigned>(zone),
                     esp_err_to_name(err));
        }
    }

    nvs_close(handle);
}

static void saveWaterUsageTotal(ZoneId zone)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("water_usage", NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open water usage NVS namespace for write: %s",
                 esp_err_to_name(err));
        return;
    }

    const uint64_t totalMl = s_waterUsage.getTotalMl(zone);
    err = nvs_set_u64(handle, waterUsageNvsKey(zone), totalMl);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to persist Zone %u water total: %s",
                 static_cast<unsigned>(zone),
                 esp_err_to_name(err));
    }

    nvs_close(handle);
}

// ── Zigbee required callback ──────────────────────────────────────────────────

extern "C" void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    const auto sig = static_cast<esp_zb_app_signal_type_t>(
        *reinterpret_cast<uint32_t *>(signal_struct->p_app_signal));

    switch (sig)
    {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initializing Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (signal_struct->esp_err_status == ESP_OK)
        {
            const bool factoryNew = esp_zb_bdb_is_factory_new();
            const bool joined = esp_zb_bdb_dev_joined();
            ESP_LOGI(TAG, "Zigbee stack started in %s factory-reset mode, joined=%s",
                     factoryNew ? "" : "non",
                     joined ? "yes" : "no");
            if (factoryNew || !joined)
            {
                ESP_LOGI(TAG, "Beginning network steering");
                s_zigbeeJoined.store(false, std::memory_order_relaxed);
                StatusLed::setState(StatusLed::State::Connecting);
                startNetworkSteering(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
            else
            {
                ZbDevice::configureReporting();
                s_zigbeeJoined.store(true, std::memory_order_relaxed);
                StatusLed::setState(StatusLed::State::Joined);
                ESP_LOGI(TAG, "Using stored Zigbee network: PAN ID 0x%04x, channel %u, short address 0x%04x",
                         esp_zb_get_pan_id(),
                         static_cast<unsigned>(esp_zb_get_current_channel()),
                         esp_zb_get_short_address());
            }
        }
        else
        {
            ESP_LOGE(TAG, "Zigbee start failed: %s",
                     esp_err_to_name(signal_struct->esp_err_status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (signal_struct->esp_err_status == ESP_OK)
        {
            ESP_LOGI(TAG, "Joined Zigbee network: PAN ID 0x%04x, channel %u, short address 0x%04x",
                     esp_zb_get_pan_id(),
                     static_cast<unsigned>(esp_zb_get_current_channel()),
                     esp_zb_get_short_address());
            ZbDevice::configureReporting();
            s_zigbeeJoined.store(true, std::memory_order_relaxed);
            StatusLed::setState(StatusLed::State::Joined);
        }
        else
        {
            ESP_LOGW(TAG, "Network steering failed: %s (0x%x); retrying",
                     esp_err_to_name(signal_struct->esp_err_status),
                     static_cast<unsigned>(signal_struct->esp_err_status));
            s_zigbeeJoined.store(false, std::memory_order_relaxed);
            StatusLed::setState(StatusLed::State::Connecting);
            esp_zb_scheduler_alarm(startNetworkSteering,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING,
                                   1000);
        }
        break;

    default:
        ESP_LOGI(TAG, "Zigbee signal: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(sig),
                 static_cast<unsigned>(sig),
                 esp_err_to_name(signal_struct->esp_err_status));
        break;
    }
}

// ── Task implementations ──────────────────────────────────────────────────────

static void zbTask(void * /*arg*/)
{
    ZbDevice::init();
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop(); // never returns
}

static void wateringTask(void * /*arg*/)
{
    // Track last-reported values so we only push Zigbee attributes on change.
    ZoneStatus lastStatus[ZONE_COUNT] = {};
    FaultCode lastReportedFault = static_cast<FaultCode>(0xFF);
    FaultCode lastLoggedFault = FaultCode::None;
    uint8_t lastReportedWatererState = 0xFFu;
    uint8_t lastReportedActiveZone = 0xFFu;
    uint64_t lastReportedWaterTotalMl[ZONE_COUNT] = {};
    for (uint8_t i = 0; i < ZONE_COUNT; ++i)
    {
        lastReportedWaterTotalMl[i] = UINT64_MAX;
    }
    uint32_t lastWaterLevelReportMs = 0;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(100));

        uint32_t nowMs = pdTICKS_TO_MS(xTaskGetTickCount());

        // Drain commands posted by the Zigbee callback.  Processing here keeps
        // the Zigbee stack task free regardless of how long FSM or I/O takes.
        ZbWateringCmd cmd;
        while (xQueueReceive(s_cmdQueue, &cmd, 0) == pdTRUE)
        {
            const auto zone = static_cast<unsigned>(cmd.zone);
            switch (cmd.type)
            {
            case ZbWateringCmd::Type::Request:
            {
                ESP_LOGI(TAG, "Zone %u ON command received (%lu s)",
                         zone,
                         static_cast<unsigned long>(cmd.durationSec));
                const ZoneStatus statusBeforeRequest = s_fsm->getZoneStatus(cmd.zone);
                const WateringRequest req{cmd.zone, cmd.durationSec, WaterSource::HaManual};
                if (s_fsm->request(req, nowMs))
                {
                    ESP_LOGI(TAG, "Zone %u watering started (%lu s)",
                             zone,
                             static_cast<unsigned long>(cmd.durationSec));
                }
                else
                {
                    const FaultCode fault = s_fsm->getLastFault();
                    if (fault == FaultCode::None)
                    {
                        ESP_LOGI(TAG, "Zone %u request ignored: watering already in progress",
                                 zone);
                        if (statusBeforeRequest == ZoneStatus::Idle)
                        {
                            ESP_LOGI(TAG, "Zone %u switch forced off because another zone is active",
                                     zone);
                            ZbDevice::reportZoneStatus(cmd.zone, ZoneStatus::Idle);
                            lastStatus[zoneIndex(cmd.zone)] = ZoneStatus::Idle;
                        }
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Zone %u request rejected: %s (%u)",
                                 zone,
                                 faultCodeName(fault),
                                 static_cast<unsigned>(fault));
                        ESP_LOGI(TAG, "Zone %u switch forced off after rejected request", zone);
                        ZbDevice::reportZoneStatus(cmd.zone, ZoneStatus::Idle);
                        lastStatus[zoneIndex(cmd.zone)] = ZoneStatus::Idle;
                    }
                }
                break;
            }

            case ZbWateringCmd::Type::Cancel:
            {
                const FaultCode faultBeforeClear = s_fsm->getLastFault();
                ESP_LOGI(TAG, "Zone %u OFF command received", zone);
                s_fsm->cancel();
                s_fsm->clearFault();
                if (faultBeforeClear != FaultCode::None)
                {
                    ESP_LOGI(TAG, "Fault clear requested via Zone %u OFF: %s (%u)",
                             zone,
                             faultCodeName(faultBeforeClear),
                             static_cast<unsigned>(faultBeforeClear));
                }
                break;
            }

            case ZbWateringCmd::Type::ClearFault:
            {
                const FaultCode faultBeforeClear = s_fsm->getLastFault();
                ESP_LOGI(TAG, "Clear fault command received");
                s_fsm->clearFault();
                ZbDevice::resetClearFaultTrigger();
                for (uint8_t i = 0; i < ZONE_COUNT; ++i)
                {
                    const auto zoneId = static_cast<ZoneId>(i + 1u);
                    ZbDevice::reportZoneStatus(zoneId, ZoneStatus::Idle);
                    lastStatus[i] = ZoneStatus::Idle;
                }
                if (faultBeforeClear != FaultCode::None)
                {
                    ESP_LOGI(TAG, "Fault clear requested: %s (%u)",
                             faultCodeName(faultBeforeClear),
                             static_cast<unsigned>(faultBeforeClear));
                }
                else
                {
                    ESP_LOGI(TAG, "Clear fault requested but no fault was latched");
                }
                break;
            }
            }
        }

        // request() can block for startup sequencing delays, so refresh the
        // timestamp before advancing the FSM in this loop iteration.
        nowMs = pdTICKS_TO_MS(xTaskGetTickCount());
        s_fsm->tick(nowMs);

        WateringDeliveryRecord delivery{};
        while (s_fsm->takeDeliveryRecord(delivery))
        {
            if (s_waterUsage.addDelivery(delivery.zone, delivery.milliliters))
            {
                const uint64_t totalMl = s_waterUsage.getTotalMl(delivery.zone);
                ESP_LOGI(TAG, "Zone %u dispensed %lu mL (lifetime %llu mL / %.3f L)",
                         static_cast<unsigned>(delivery.zone),
                         static_cast<unsigned long>(delivery.milliliters),
                         static_cast<unsigned long long>(totalMl),
                         static_cast<double>(s_waterUsage.getTotalLiters(delivery.zone)));
                saveWaterUsageTotal(delivery.zone);
                lastReportedWaterTotalMl[zoneIndex(delivery.zone)] = UINT64_MAX;
            }
            else
            {
                ESP_LOGI(TAG, "Zone %u dispensed 0 mL",
                         static_cast<unsigned>(delivery.zone));
            }
        }

        const FaultCode fault = s_fsm->getLastFault();
        ZoneStatus currentStatus[ZONE_COUNT] = {};
        for (uint8_t i = 0; i < ZONE_COUNT; ++i)
        {
            currentStatus[i] = s_fsm->getZoneStatus(static_cast<ZoneId>(i + 1u));
        }

        // Push aggregate fault/state first so HA sees the high-level status
        // change before the per-zone endpoint churn that accompanies it.
        if (fault != lastLoggedFault)
        {
            if (fault == FaultCode::None)
            {
                ESP_LOGI(TAG, "Fault cleared");
            }
            else
            {
                ESP_LOGW(TAG, "Fault raised: %s (%u)",
                         faultCodeName(fault),
                         static_cast<unsigned>(fault));
            }
            lastLoggedFault = fault;
        }
        if (ZbDevice::criticalReportsEnabled() && fault != lastReportedFault)
        {
            lastReportedFault = fault;
            ZbDevice::reportFault(fault);
        }

        const WatererStateCode watererState = watererStateFromStatuses(currentStatus, fault);
        const uint8_t watererStateCode = static_cast<uint8_t>(watererState);
        const uint8_t activeZone = activeZoneFromStatuses(currentStatus);
        if (ZbDevice::criticalReportsEnabled() && watererStateCode != lastReportedWatererState)
        {
            ESP_LOGI(TAG, "Waterer state: %s", watererStateName(watererState));
            lastReportedWatererState = watererStateCode;
            ZbDevice::reportWatererState(watererStateCode);
        }
        if (ZbDevice::criticalReportsEnabled() && activeZone != lastReportedActiveZone)
        {
            ESP_LOGI(TAG, "Active zone: %u", static_cast<unsigned>(activeZone));
            lastReportedActiveZone = activeZone;
            ZbDevice::reportActiveZone(activeZone);
        }

        // Report zone status changes after the aggregate status has been sent.
        for (uint8_t i = 0; i < ZONE_COUNT; ++i)
        {
            const auto zoneId = static_cast<ZoneId>(i + 1u);
            const auto status = currentStatus[i];
            if (status != lastStatus[i])
            {
                ESP_LOGI(TAG, "Zone %u status: %s -> %s",
                         static_cast<unsigned>(zoneId),
                         zoneStatusName(lastStatus[i]),
                         zoneStatusName(status));
                lastStatus[i] = status;
                ZbDevice::reportZoneStatus(zoneId, status);
            }
        }

        if (ZbDevice::reportsEnabled())
        {
            for (uint8_t i = 0; i < ZONE_COUNT; ++i)
            {
                const auto zoneId = static_cast<ZoneId>(i + 1u);
                const uint64_t totalMl = s_waterUsage.getTotalMl(zoneId);
                if (totalMl != lastReportedWaterTotalMl[i])
                {
                    lastReportedWaterTotalMl[i] = totalMl;
                    ZbDevice::reportZoneWaterTotal(zoneId, totalMl);
                }
            }
        }

        if (ZbDevice::reportsEnabled() &&
            (lastWaterLevelReportMs == 0 ||
             nowMs - lastWaterLevelReportMs >= config::renogy::POLL_INTERVAL_MS))
        {
            lastWaterLevelReportMs = nowMs;
            const WaterLevelReading water = s_tank->getReading();
            ESP_LOGI(TAG, "Water level: %u%% (%.0f mV)",
                     water.percent,
                     water.millivolts);
            ZbDevice::reportWaterLevel(water.percent);
        }

        // Drive LED: fault > watering > joined heartbeat > connecting blink.
        if (fault != FaultCode::None)
        {
            StatusLed::setState(StatusLed::State::Fault);
        }
        else
        {
            const bool anyActive = [&]()
            {
                for (uint8_t i = 0; i < ZONE_COUNT; ++i)
                {
                    if (lastStatus[i] == ZoneStatus::Running ||
                        lastStatus[i] == ZoneStatus::Priming)
                        return true;
                }
                return false;
            }();
            if (anyActive)
            {
                StatusLed::setState(StatusLed::State::Watering);
            }
            else if (s_zigbeeJoined.load(std::memory_order_relaxed))
            {
                StatusLed::setState(StatusLed::State::Joined);
            }
            else
            {
                StatusLed::setState(StatusLed::State::Connecting);
            }
        }
    }
}

static void renogyTask(void * /*arg*/)
{
    // Poll immediately so fresh battery/solar data is available before the
    // first watering request arrives.
    if (!s_renogy.poll())
    {
        ESP_LOGW(TAG, "Renogy initial poll failed");
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(config::renogy::POLL_INTERVAL_MS));

        if (s_renogy.poll())
        {
            const RenogyData d = s_renogy.getData();
            ZbDevice::reportBattery(static_cast<uint8_t>(d.batterySoc), d.batteryVoltage);
            ZbDevice::reportSolarData(d.batteryVoltage,
                                      d.pvVoltage,
                                      d.pvPower,
                                      d.controllerTemp,
                                      d.maxChargingPowerToday,
                                      d.dailyGenerationWh,
                                      d.dailyConsumptionWh,
                                      d.chargingStatus);
            ESP_LOGI(TAG, "Renogy poll OK: SOC %u%% %.1fV PV %.1fV %uW maxChg=%uW gen=%uWh con=%uWh status=%u",
                     d.batterySoc, d.batteryVoltage, d.pvVoltage, d.pvPower,
                     d.maxChargingPowerToday, d.dailyGenerationWh, d.dailyConsumptionWh,
                     d.chargingStatus);
        }
        else
        {
            ESP_LOGW(TAG, "Renogy poll failed");
        }
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

extern "C" void app_main(void)
{
    // NVS required by the Zigbee stack.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    loadWaterUsageTotals();

    esp_zb_platform_config_t zbPlatform = {};
    zbPlatform.radio_config.radio_mode = ZB_RADIO_MODE_NATIVE;
    zbPlatform.host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE;
    ESP_ERROR_CHECK(esp_zb_platform_config(&zbPlatform));

    // ── HAL init ──────────────────────────────────────────────────────────────

    // Status LED — init early so it blinks Connecting state while Zigbee joins.
    StatusLed::init();

    // LEDC timer — must be configured before any LedcPwm channel is used.
    // The static LedcPwm objects above call ledc_channel_config() in their
    // constructors; channel config is harmless before the timer is ready,
    // but we want the timer active before any setDutyPercent() call.
    LedcPwm::initTimer();

    // ADC unit + calibration for ADC1.
    const adc_oneshot_unit_handle_t adc1 = EspAdcChannel::createUnit();
    const adc_cali_handle_t cali1 = EspAdcChannel::createCali(ADC_UNIT_1,
                                                              ADC_ATTEN_DB_12);

    EspAdcChannel::configChannel(adc1, ADC_CHANNEL_0); // float sensor

    // ADC channel objects (heap-allocated; live forever).
    s_floatAdcCh = new EspAdcChannel(adc1, ADC_CHANNEL_0, cali1, config::adc::OVERSAMPLE_COUNT);

    // Pump and tank sensor.
    s_pump = new PumpActuator(s_pumpPwm);
    s_tank = new FloatSensor(*s_floatAdcCh);

    // Zone manager and FSM.
    s_zones = new ZoneManager({&s_sol1, &s_sol2, &s_sol3, &s_sol4, &s_sol5});
    s_fsm = new WateringFsm(*s_zones, *s_pump, s_flow, *s_tank, s_renogy);

    // Enable the BTS7960 master-enable line (all three boards).
    s_drvMasterEn.setHigh();
    ESP_LOGI(TAG, "BTS7960 master enable HIGH");

    // ── Zigbee handler wiring ─────────────────────────────────────────────────

    s_cmdQueue = xQueueCreate(4, sizeof(ZbWateringCmd));
    configASSERT(s_cmdQueue);

    ZbHandlers::init(s_cmdQueue);

    // ── Tasks ─────────────────────────────────────────────────────────────────

    xTaskCreate(zbTask, "zb", 6144, nullptr, 5, nullptr);
    xTaskCreate(wateringTask, "water", 4096, nullptr, 3, nullptr);
    xTaskCreate(renogyTask, "renogy", 3072, nullptr, 2, nullptr);
    xTaskCreate(StatusLed::runTask, "led", 2048, nullptr, 1, nullptr);

    ESP_LOGI(TAG, "All tasks started");
}
