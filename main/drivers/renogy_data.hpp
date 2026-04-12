#pragma once
#include <cstdint>

// Plain data snapshot from the Renogy Wanderer 10A solar charge controller.
// Real-time data: registers 0x0100–0x0109.
// Historical/status data: registers 0x010B–0x0114, 0x0120.
// Written exclusively by RenogyDriver::poll() under its mutex.
// Read by any task via RenogyDriver::getData() which returns a copy.
struct RenogyData {
    // ── Real-time telemetry ───────────────────────────────────────────────────
    uint16_t batterySoc;              // %      (reg 0x0100, raw value = % directly, 0–100)
    float    batteryVoltage;          // V      (reg 0x0101 × 0.1)
    float    batteryCurrent;          // A      (reg 0x0102 × 0.01, signed — positive = charging)
    float    pvVoltage;               // V      (reg 0x0107 × 0.1)
    float    pvCurrent;               // A      (reg 0x0108 × 0.01)
    uint16_t pvPower;                 // W      (reg 0x0109)
    uint16_t loadPower;               // W      (reg 0x0106)
    float    controllerTemp;          // °C     (reg 0x0103 high byte, sign bit 7)

    // ── Daily historical data (reset at midnight by the controller) ───────────
    uint16_t maxChargingPowerToday;   // W      (reg 0x010F, actual value)
    uint16_t dailyGenerationWh;       // Wh     (reg 0x0113, raw in kWh/1000 = Wh)
    uint16_t dailyConsumptionWh;      // Wh     (reg 0x0114, raw in kWh/1000 = Wh)

    // ── Charging status (reg 0x0120 low byte) ────────────────────────────────
    // 0=not started, 1=startup, 2=MPPT, 3=equalisation, 4=boost, 5=float, 6=current limiting
    uint8_t  chargingStatus;

    uint32_t lastUpdateMs;            // pdTICKS_TO_MS(xTaskGetTickCount()) at last good poll
};
