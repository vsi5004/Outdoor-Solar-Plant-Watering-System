#include "watering/watering_fsm.hpp"
#include "config.hpp"

// ── Construction ──────────────────────────────────────────────────────────────

WateringFsm::WateringFsm(IZoneManager&   zones,
                         IPumpActuator&  pump,
                         IFlowMeter&     flow,
                         IFloatSensor&   tank,
                         IRenogyMonitor& renogy)
    : zones_(zones)
    , pump_(pump)
    , flow_(flow)
    , tank_(tank)
    , renogy_(renogy)
{
}

// ── Public API ────────────────────────────────────────────────────────────────

bool WateringFsm::request(const WateringRequest& req, uint32_t nowMs)
{
    if (state_ != State::Idle) {
        return false;
    }
    if (!isValidZoneId(req.zone) || req.durationSec == 0u) {
        enterFault(FaultCode::InvalidRequest);
        return false;
    }
    if (renogy_.getData().batterySoc <= config::safety::MIN_BATTERY_SOC_PCT) {
        enterFault(FaultCode::LowBattery);
        return false;
    }
    if (tank_.getPercent() <= config::safety::MIN_WATER_LEVEL_PCT) {
        enterFault(FaultCode::LowWater);
        return false;
    }

    if (!renogy_.setLoad(true)) {
        enterFault(FaultCode::LoadEnableFailed);
        return false;
    }

    req_              = req;
    targetDurationMs_ = static_cast<uint32_t>(req.durationSec) * 1000u;
    deliveredMl_      = 0u;

    flow_.reset();
    zones_.open(req_.zone);
    pump_.setSpeed(100u);
    phaseStartMs_ = nowMs;
    state_        = State::Priming;
    return true;
}

void WateringFsm::tick(uint32_t nowMs)
{
    const uint32_t elapsed = nowMs - phaseStartMs_;

    switch (state_) {

    case State::Priming:
        if (flow_.getPulses() >= static_cast<int32_t>(config::pump::PRIME_PULSE_COUNT)) {
            phaseStartMs_ = nowMs;
            state_        = State::Watering;
        } else if (elapsed >= config::pump::PRIME_TIMEOUT_MS) {
            enterFault(FaultCode::PrimeTimeout);
        }
        break;

    case State::Watering:
        if (elapsed >= config::pump::MAX_DISPENSE_MS) {
            enterFault(FaultCode::MaxDuration);
            break;
        }
        if (elapsed >= targetDurationMs_) {
            deliveredMl_ = static_cast<uint32_t>(flow_.getMilliliters() + 0.5f);
            stopAll();
            state_ = State::Idle;
            break;
        }
        if (pump_.readCurrentMa() < config::pump::DRY_RUN_MA) {
            enterFault(FaultCode::DryRun);
            break;
        }
        if (renogy_.getData().batterySoc <= config::safety::MIN_BATTERY_SOC_PCT) {
            enterFault(FaultCode::LowBattery);
            break;
        }
        break;

    case State::Idle:
    case State::Fault:
        break;
    }
}

ZoneStatus WateringFsm::getZoneStatus(ZoneId id) const
{
    if (id != req_.zone) return ZoneStatus::Idle;

    switch (state_) {
    case State::Priming:  return ZoneStatus::Priming;
    case State::Watering: return ZoneStatus::Running;
    case State::Fault:    return ZoneStatus::Fault;
    default:              return ZoneStatus::Idle;
    }
}

FaultCode WateringFsm::getLastFault() const  { return fault_; }
uint32_t  WateringFsm::getDeliveredMl() const { return deliveredMl_; }

void WateringFsm::clearFault()
{
    if (state_ == State::Fault) {
        fault_ = FaultCode::None;
        state_ = State::Idle;
    }
}

void WateringFsm::cancel()
{
    if (state_ == State::Priming || state_ == State::Watering) {
        deliveredMl_ = static_cast<uint32_t>(flow_.getMilliliters() + 0.5f);
        stopAll();
        state_ = State::Idle;
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void WateringFsm::stopAll()
{
    pump_.stop();
    zones_.closeAll();
    renogy_.setLoad(false);
}

void WateringFsm::enterFault(FaultCode code)
{
    deliveredMl_ = static_cast<uint32_t>(flow_.getMilliliters() + 0.5f);
    stopAll();
    fault_ = code;
    state_ = State::Fault;
}
