#pragma once
#include <cstdint>

// Plain data snapshot from the Renogy Wanderer solar charge controller.
// Written exclusively by RenogyDriver::poll() under its mutex.
// Read by any task via RenogyDriver::getData() which returns a copy.
struct RenogyData {
    uint16_t batterySoc      = 0;   // %
    float    batteryVoltage  = 0.f; // V  (register value × 0.1)
    float    chargingCurrent = 0.f; // A  (register value × 0.01)
    float    pvVoltage       = 0.f; // V  (register value × 0.1)
    uint16_t chargingPower   = 0;   // W
    float    loadCurrent     = 0.f; // A  (register value × 0.01)
    int8_t   controllerTemp  = 0;   // °C (high byte of register 0x0107)
    uint32_t lastUpdateMs    = 0;   // pdTICKS_TO_MS(xTaskGetTickCount()) at last good poll
};
