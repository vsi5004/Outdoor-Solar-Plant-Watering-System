#include "hal/esp_adc_channel.hpp"
#include "esp_log.h"

static const char* TAG = "EspAdcChannel";

// ── Instance ──────────────────────────────────────────────────────────────────

EspAdcChannel::EspAdcChannel(adc_oneshot_unit_handle_t unit,
                             adc_channel_t             ch,
                             adc_cali_handle_t         cali,
                             uint8_t                   samples)
    : unit_(unit), ch_(ch), cali_(cali), samples_(samples ? samples : 1)
{
}

float EspAdcChannel::readMillivolts()
{
    int32_t sum = 0;
    for (uint8_t i = 0; i < samples_; ++i) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(unit_, ch_, &raw));
        sum += raw;
    }
    const int raw_avg = static_cast<int>(sum / samples_);

    if (cali_) {
        int mv = 0;
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_, raw_avg, &mv));
        return static_cast<float>(mv);
    }
    // No calibration — rough linear approximation for 12 dB (0–3100 mV range).
    return static_cast<float>(raw_avg) * (3100.0f / 4095.0f);
}

// ── Unit-level helpers ────────────────────────────────────────────────────────

adc_oneshot_unit_handle_t EspAdcChannel::createUnit()
{
    adc_oneshot_unit_init_cfg_t cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_unit_handle_t handle = nullptr;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&cfg, &handle));
    return handle;
}

adc_cali_handle_t EspAdcChannel::createCali(adc_unit_t unit, adc_atten_t atten)
{
    adc_cali_handle_t handle = nullptr;
    esp_err_t         err    = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = unit,
        .chan     = ADC_CHANNEL_0, // channel unused for unit-level calibration
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cfg, &handle);
    if (err == ESP_OK) {
        return handle;
    }
    ESP_LOGW(TAG, "Curve fitting calibration unavailable (%s), trying line fitting",
             esp_err_to_name(err));
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_cfg = {
        .unit_id  = unit,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&line_cfg, &handle);
    if (err == ESP_OK) {
        return handle;
    }
    ESP_LOGW(TAG, "Line fitting calibration unavailable (%s)", esp_err_to_name(err));
#endif

    return nullptr;
}

void EspAdcChannel::configChannel(adc_oneshot_unit_handle_t unit,
                                  adc_channel_t             ch,
                                  adc_atten_t               atten)
{
    adc_oneshot_chan_cfg_t cfg = {
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(unit, ch, &cfg));
}
