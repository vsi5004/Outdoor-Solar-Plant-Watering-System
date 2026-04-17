#include "watering/watering_fsm.hpp"
#include "config.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{

    static const char *TAG = "WateringFsm";

    constexpr uint32_t LOAD_ENABLE_SETTLE_MS = config::renogy::LOAD_ENABLE_SETTLE_MS;
    constexpr uint32_t PUMP_STOP_TO_SOLENOID_CLOSE_MS =
        config::watering_sequence::PUMP_STOP_TO_SOLENOID_CLOSE_MS;
    constexpr uint32_t SOLENOID_CLOSE_TO_LOAD_DISABLE_MS =
        config::watering_sequence::SOLENOID_CLOSE_TO_LOAD_DISABLE_MS;

    void delayForTaskMs(uint32_t delayMs)
    {
#if CONFIG_IDF_TARGET_LINUX
        (void)delayMs;
#else
        if (delayMs > 0u)
        {
            vTaskDelay(pdMS_TO_TICKS(delayMs));
        }
#endif
    }

    uint32_t getTaskNowMs(uint32_t fallbackNowMs)
    {
#if CONFIG_IDF_TARGET_LINUX
        return fallbackNowMs;
#else
        return static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
#endif
    }

} // namespace

// ── Construction ──────────────────────────────────────────────────────────────

WateringFsm::WateringFsm(IZoneManager &zones,
                         IPumpActuator &pump,
                         IFlowMeter &flow,
                         IFloatSensor &tank,
                         IRenogyMonitor &renogy)
    : zones_(zones), pump_(pump), flow_(flow), tank_(tank), renogy_(renogy)
{
}

// ── Public API ────────────────────────────────────────────────────────────────

bool WateringFsm::request(const WateringRequest &req, uint32_t nowMs)
{
    if (state_ != State::Idle)
    {
        return false;
    }
    zoneOwned_ = false;
    if (!isValidZoneId(req.zone) || req.durationSec == 0u)
    {
        enterFault(FaultCode::InvalidRequest);
        return false;
    }
    const RenogyData renogySnap = renogy_.getData();
    if (!renogySnap.valid ||
        (renogySnap.lastUpdateMs > 0u &&
         (nowMs - renogySnap.lastUpdateMs) > config::renogy::STALE_THRESHOLD_MS))
    {
        enterFault(FaultCode::StaleData);
        return false;
    }
    if (renogySnap.batterySoc <= config::safety::MIN_BATTERY_SOC_PCT)
    {
        enterFault(FaultCode::LowBattery);
        return false;
    }
    if (tank_.getPercent() <= config::safety::MIN_WATER_LEVEL_PCT)
    {
        enterFault(FaultCode::LowWater);
        return false;
    }

    if (!renogy_.setLoad(true))
    {
        enterFault(FaultCode::LoadEnableFailed);
        return false;
    }
    const uint32_t loadCommandAckMs = getTaskNowMs(nowMs);
    ESP_LOGI(TAG, "Zone %u: load command acknowledged, waiting %lu ms before opening solenoid",
             static_cast<unsigned>(req.zone),
             static_cast<unsigned long>(LOAD_ENABLE_SETTLE_MS));
    delayForTaskMs(LOAD_ENABLE_SETTLE_MS);
    const uint32_t solenoidOpenMs = getTaskNowMs(loadCommandAckMs);
    ESP_LOGI(TAG, "Zone %u: opening solenoid after %lu ms command-to-solenoid delay",
             static_cast<unsigned>(req.zone),
             static_cast<unsigned long>(solenoidOpenMs - loadCommandAckMs));

    req_ = req;
    targetDurationMs_ = static_cast<uint32_t>(req.durationSec) * 1000u;
    deliveredMl_ = 0u;
    zoneOwned_ = true;

    flow_.reset();
    zones_.open(req_.zone);
    const uint32_t pumpStartMs = getTaskNowMs(solenoidOpenMs);
    pump_.setSpeed(config::pump::DUTY_PCT);
    ESP_LOGI(TAG, "Zone %u: starting pump %lu ms after solenoid-open step began",
             static_cast<unsigned>(req.zone),
             static_cast<unsigned long>(pumpStartMs - solenoidOpenMs));
    phaseStartMs_ = pumpStartMs;
    state_ = State::Priming;
    return true;
}

void WateringFsm::tick(uint32_t nowMs)
{
    const uint32_t elapsed = nowMs - phaseStartMs_;

    switch (state_)
    {

    case State::Priming:
        if (flow_.getPulses() >= static_cast<int32_t>(config::pump::PRIME_PULSE_COUNT))
        {
            phaseStartMs_ = nowMs;
            state_ = State::Watering;
        }
        else if (elapsed >= config::pump::PRIME_TIMEOUT_MS)
        {
            enterFault(FaultCode::PrimeTimeout);
        }
        break;

    case State::Watering:
        if (elapsed >= config::pump::MAX_DISPENSE_MS)
        {
            enterFault(FaultCode::MaxDuration);
            break;
        }
        if (elapsed >= targetDurationMs_)
        {
            captureDelivery();
            stopAll();
            zoneOwned_ = false;
            state_ = State::Idle;
            break;
        }
        {
            const RenogyData d = renogy_.getData();
            if (d.lastUpdateMs > 0u &&
                (nowMs - d.lastUpdateMs) > config::renogy::STALE_THRESHOLD_MS)
            {
                enterFault(FaultCode::StaleData);
                break;
            }
            if (d.batterySoc <= config::safety::MIN_BATTERY_SOC_PCT)
            {
                enterFault(FaultCode::LowBattery);
                break;
            }
        }
        break;

    case State::Idle:
    case State::Fault:
        break;
    }
}

ZoneStatus WateringFsm::getZoneStatus(ZoneId id) const
{
    if (!zoneOwned_)
        return ZoneStatus::Idle;
    if (id != req_.zone)
        return ZoneStatus::Idle;

    switch (state_)
    {
    case State::Priming:
        return ZoneStatus::Priming;
    case State::Watering:
        return ZoneStatus::Running;
    case State::Fault:
        return ZoneStatus::Fault;
    default:
        return ZoneStatus::Idle;
    }
}

FaultCode WateringFsm::getLastFault() const { return fault_; }
uint32_t WateringFsm::getDeliveredMl() const { return deliveredMl_; }

bool WateringFsm::takeDeliveryRecord(WateringDeliveryRecord &record)
{
    if (!hasPendingDelivery_)
    {
        return false;
    }

    record = pendingDelivery_;
    hasPendingDelivery_ = false;
    return true;
}

void WateringFsm::clearFault()
{
    if (state_ == State::Fault)
    {
        fault_ = FaultCode::None;
        zoneOwned_ = false;
        state_ = State::Idle;
    }
}

void WateringFsm::cancel()
{
    if (state_ == State::Priming || state_ == State::Watering)
    {
        captureDelivery();
        stopAll();
        zoneOwned_ = false;
        state_ = State::Idle;
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void WateringFsm::stopAll()
{
    stopOutputs(true, true);
}

void WateringFsm::stopOutputs(bool useShutdownDelays, bool logSteps)
{
    if (logSteps)
    {
        ESP_LOGI(TAG, "Stopping pump");
    }
    pump_.stop();

    if (useShutdownDelays)
    {
        if (logSteps)
        {
            ESP_LOGI(TAG, "Waiting %lu ms before closing solenoid",
                     static_cast<unsigned long>(PUMP_STOP_TO_SOLENOID_CLOSE_MS));
        }
        delayForTaskMs(PUMP_STOP_TO_SOLENOID_CLOSE_MS);
    }

    if (logSteps)
    {
        ESP_LOGI(TAG, "Closing all solenoids");
    }
    zones_.closeAll();

    if (useShutdownDelays)
    {
        if (logSteps)
        {
            ESP_LOGI(TAG, "Waiting %lu ms before disabling Renogy load",
                     static_cast<unsigned long>(SOLENOID_CLOSE_TO_LOAD_DISABLE_MS));
        }
        delayForTaskMs(SOLENOID_CLOSE_TO_LOAD_DISABLE_MS);
    }

    if (logSteps)
    {
        ESP_LOGI(TAG, "Disabling Renogy load");
    }
    renogy_.setLoad(false);
}

void WateringFsm::captureDelivery()
{
    const bool acceptedCycle =
        zoneOwned_ || state_ == State::Priming || state_ == State::Watering;
    if (!acceptedCycle)
    {
        deliveredMl_ = 0u;
        return;
    }

    deliveredMl_ = static_cast<uint32_t>(flow_.getMilliliters() + 0.5f);
    if (zoneOwned_ && isValidZoneId(req_.zone))
    {
        pendingDelivery_ = {req_.zone, deliveredMl_};
        hasPendingDelivery_ = true;
    }
}

void WateringFsm::enterFault(FaultCode code)
{
    captureDelivery();
    const bool outputsMayBeActive =
        zoneOwned_ || state_ == State::Priming || state_ == State::Watering;
    stopOutputs(outputsMayBeActive, outputsMayBeActive);
    fault_ = code;
    state_ = State::Fault;
}
