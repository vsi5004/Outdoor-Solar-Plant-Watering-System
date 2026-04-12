#include "unity.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "hal/esp_adc_channel.hpp"
#include "drivers/float_sensor.hpp"
#include "config.hpp"

static const char* TAG = "test_float_sensor";

// ---------------------------------------------------------------------------
// HIL test — float sensor (GPIO0, ADC1_CH0)
//
// Logs raw mV and computed water level percent.  Calibration constants in
// config.hpp are placeholders — observe the mV readings at empty and full
// reservoir to update FLOAT_EMPTY_MV / FLOAT_FULL_MV.
// ---------------------------------------------------------------------------

// Called from test_runner.cpp to force the linker to pull in this translation
// unit so its TEST_CASE registrations are not silently dropped.
void register_float_sensor_tests() {}

TEST_CASE("float sensor: log raw mV and water level percent", "[float_sensor]")
{
    adc_oneshot_unit_handle_t unit = EspAdcChannel::createUnit();
    adc_cali_handle_t cali = EspAdcChannel::createCali(ADC_UNIT_1, ADC_ATTEN_DB_12);
    EspAdcChannel::configChannel(unit,
                                 static_cast<adc_channel_t>(config::adc::FLOAT_SENSOR_CH),
                                 ADC_ATTEN_DB_12);

    EspAdcChannel adcCh(unit, static_cast<adc_channel_t>(config::adc::FLOAT_SENSOR_CH), cali,
                        config::adc::OVERSAMPLE_COUNT);
    FloatSensor   sensor(adcCh,
                         config::sensor::FLOAT_EMPTY_MV,
                         config::sensor::FLOAT_FULL_MV);

    const float   mv  = adcCh.readMillivolts();
    const uint8_t pct = sensor.getPercent();

    ESP_LOGI(TAG, "raw: %.1f mV  |  computed: %u %%", mv, pct);

    // Teardown — release hardware so the test can be run again in the same session.
    adc_oneshot_del_unit(unit);
    if (cali) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(cali);
#endif
    }
}
