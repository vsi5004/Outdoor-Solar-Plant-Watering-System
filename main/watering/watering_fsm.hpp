#pragma once
#include <cstdint>
#include "watering/izone_manager.hpp"
#include "watering/watering_request.hpp"
#include "watering/zone_status.hpp"
#include "watering/fault_code.hpp"
#include "drivers/ipump_actuator.hpp"
#include "drivers/iflow_meter.hpp"
#include "drivers/ifloat_sensor.hpp"
#include "drivers/irenogy_monitor.hpp"

// Finite state machine for a single-zone watering cycle.
//
// States:
//   Idle     — waiting for a request
//   Priming  — pump running, waiting for flow pulses to confirm prime
//   Watering — dispensing; monitors time, battery, and pump current
//   Fault    — all outputs off; call clearFault() to return to Idle
//
// Callers pass the current time (ms) explicitly to request() and tick().
// In production: pdTICKS_TO_MS(xTaskGetTickCount()).
// In tests: a simple counter, giving full deterministic control over timing.
class WateringFsm {
public:
    WateringFsm(IZoneManager&   zones,
                IPumpActuator&  pump,
                IFlowMeter&     flow,
                IFloatSensor&   tank,
                IRenogyMonitor& renogy);

    // Submit a watering request. Ignored unless FSM is Idle.
    // Returns true and enters Priming on success.
    // Returns false and enters Fault if a precondition fails.
    bool request(const WateringRequest& req, uint32_t nowMs);

    // Advance the state machine. Call at regular intervals.
    void tick(uint32_t nowMs);

    // Per-zone status for Zigbee endpoint reporting (EP 10–14).
    ZoneStatus getZoneStatus(ZoneId id) const;

    // Last fault code. FaultCode::None when Idle or running normally.
    FaultCode getLastFault() const;

    // Volume delivered (ml) in the most recently completed or aborted cycle.
    uint32_t getDeliveredMl() const;

    // Transition Fault → Idle. No-op in any other state.
    void clearFault();

    // Cancel an active cycle (Priming or Watering) and return to Idle.
    // Records partial deliveredMl. No-op in Idle or Fault.
    void cancel();

private:
    enum class State : uint8_t { Idle, Priming, Watering, Fault };

    void stopAll();
    void enterFault(FaultCode code);

    IZoneManager&   zones_;
    IPumpActuator&  pump_;
    IFlowMeter&     flow_;
    IFloatSensor&   tank_;
    IRenogyMonitor& renogy_;

    State           state_            = State::Idle;
    FaultCode       fault_            = FaultCode::None;
    WateringRequest req_              = {};
    uint32_t        phaseStartMs_     = 0;
    uint32_t        targetDurationMs_ = 0;
    uint32_t        deliveredMl_      = 0;
};
