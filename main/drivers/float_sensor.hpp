#pragma once
#include <cstdint>
#include "drivers/ifloat_sensor.hpp"
#include "hal/iadc_channel.hpp"
#include "config.hpp"

struct WaterLevelReading {
    float   millivolts;
    uint8_t percent;
};

// Reads the variable-resistance float sensor and returns a calibrated water
// level percentage.
//
// Calibration:
//   emptyMv — ADC reading (mV) when the reservoir is completely empty.
//   fullMv  — ADC reading (mV) when the reservoir is completely full.
//
// Both default to the config::sensor placeholders. Update those values (or
// pass them explicitly) after measuring the sensor at each extreme. Consider
// persisting calibrated values to NVS so they survive reboots.
//
// Readings outside [emptyMv, fullMv] are clamped to [0, 100].
class FloatSensor : public IFloatSensor {
public:
    explicit FloatSensor(IAdcChannel& adc,
                         float emptyMv = config::sensor::FLOAT_EMPTY_MV,
                         float fullMv  = config::sensor::FLOAT_FULL_MV);

    // Trigger an ADC conversion and return water level as 0–100 %.
    uint8_t getPercent() const override;

    // Trigger one ADC conversion and return both raw millivolts and calibrated
    // percentage. Useful for logging/calibration without sampling twice.
    WaterLevelReading getReading() const;

private:
    IAdcChannel& adc_;
    float        emptyMv_;
    float        fullMv_;
};
