#pragma once
#include "hal/iadc_channel.hpp"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

// IAdcChannel backed by the ESP-IDF ADC oneshot + calibration APIs.
//
// The ADC unit handle and calibration handle are created once in app_main and
// shared across all channels on the same unit.  Each EspAdcChannel instance
// wraps one physical channel on that unit.
class EspAdcChannel : public IAdcChannel {
public:
    // unit     — shared handle created by adc_oneshot_new_unit()
    // ch       — ADC_CHANNEL_0 … ADC_CHANNEL_9
    // cali     — optional calibration handle (pass nullptr to skip calibration;
    //            readMillivolts() will return raw counts scaled to mV without curve
    //            compensation, which is only acceptable for rough readings)
    // samples  — number of conversions to average per readMillivolts() call;
    //            higher values reduce noise at the cost of slightly more time.
    //            4 is a good default for resistive sensors.
    EspAdcChannel(adc_oneshot_unit_handle_t unit,
                  adc_channel_t             ch,
                  adc_cali_handle_t         cali,
                  uint8_t                   samples);

    // Trigger `samples` conversions, average them, and return the calibrated
    // voltage in mV.
    float readMillivolts() override;

    // ── Unit-level helpers (call once in app_main) ───────────────────────────

    // Create and return an ADC1 unit handle.
    static adc_oneshot_unit_handle_t createUnit();

    // Create a calibration handle for the given unit and attenuation.
    // Returns nullptr if the hardware does not support curve fitting and
    // line fitting is not available — in that case pass nullptr to ctor.
    static adc_cali_handle_t createCali(adc_unit_t unit, adc_atten_t atten);

    // Configure a channel on the unit (must be called before first read).
    static void configChannel(adc_oneshot_unit_handle_t unit,
                              adc_channel_t             ch,
                              adc_atten_t               atten = ADC_ATTEN_DB_12);

private:
    adc_oneshot_unit_handle_t unit_;
    adc_channel_t             ch_;
    adc_cali_handle_t         cali_;
    uint8_t                   samples_;
};
