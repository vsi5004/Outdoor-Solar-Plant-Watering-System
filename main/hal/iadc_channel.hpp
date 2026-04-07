#pragma once

// Single ADC input channel, returning a calibrated voltage.
// Implementations use the ESP-IDF adc_oneshot + adc_cali APIs.
// readMillivolts() is non-const because a hardware ADC read is a side-effecting
// operation (it triggers a conversion).
class IAdcChannel {
public:
    virtual ~IAdcChannel() = default;

    // Trigger a conversion and return the calibrated voltage in millivolts.
    virtual float readMillivolts() = 0;
};
