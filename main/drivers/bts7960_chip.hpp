#pragma once
#include "hal/iadc_channel.hpp"

// Represents one physical BTS7960 H-bridge IC.
//
// Each chip has a single IS (current sense) pin shared across both half-bridges.
// readCurrentMa() returns the combined load current of whichever half-bridges
// are active. Callers that need per-half current must account for this.
//
// IS sense formula (1 kΩ sense resistor to GND):
//   I_IS   = I_load / 8500          (mirror ratio, per datasheet)
//   V_IS   = I_IS * 1000 Ω
//   V_mV   = I_load_mA * 1000 / 8500
//   → I_load_mA = V_mV * IS_MV_TO_MA
class Bts7960Chip {
public:
    // IS mirror ratio with 1 kΩ sense resistor: mA = V_IS_mV * IS_MV_TO_MA
    static constexpr float IS_MV_TO_MA = 8.5f;

    explicit Bts7960Chip(IAdcChannel& isPin);

    // Trigger an ADC conversion and return the total load current in mA.
    float readCurrentMa() const;

private:
    IAdcChannel& isPin_;
};
