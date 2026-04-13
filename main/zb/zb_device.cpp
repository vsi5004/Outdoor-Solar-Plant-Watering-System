#include "zb/zb_device.hpp"
#include "zb/zb_handlers.hpp"
#include "watering/zone_id.hpp"
#include <esp_log.h>

static const char* TAG = "ZbDevice";

// ── Public API ────────────────────────────────────────────────────────────────

void ZbDevice::init()
{
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role         = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = false,
        .nwk_cfg = {
            .zczr_cfg = {
                .max_children = 10,
            },
        },
    };
    esp_zb_init(&zb_cfg);

    esp_zb_ep_list_t* ep_list = esp_zb_ep_list_create();
    buildBasicEp(ep_list);
    buildZoneEps(ep_list);
    buildBatteryEp(ep_list);
    buildSolarDataEps(ep_list);
    buildFaultEp(ep_list);
    buildChargingStatusEp(ep_list);
    esp_zb_device_register(ep_list);

    esp_zb_core_action_handler_register(ZbHandlers::onAction);

    ESP_LOGI(TAG, "Registered EP1, EP10-14, EP20-23, EP41-42");
}

void ZbDevice::reportZoneStatus(ZoneId zone, ZoneStatus status)
{
    const uint8_t ep = zoneEp(zone);
    uint8_t       on = (status == ZoneStatus::Running || status == ZoneStatus::Priming) ? 1u : 0u;

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(ep,
        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
        &on,
        false);
    esp_zb_lock_release();
}

void ZbDevice::reportBattery(uint8_t socPct, float voltageV)
{
    // ZCL Power Configuration cluster:
    //   BatteryPercentageRemaining: uint8, units 0.5 % → multiply SOC by 2
    //   BatteryVoltage:             uint8, units 100 mV → multiply V by 10
    uint8_t zbPct     = static_cast<uint8_t>(socPct * 2u);
    uint8_t zbVoltage = static_cast<uint8_t>(voltageV * 10.0f + 0.5f);

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(kBatteryEp,
        ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
        &zbPct,
        false);
    esp_zb_zcl_set_attribute_val(kBatteryEp,
        ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
        &zbVoltage,
        false);
    esp_zb_lock_release();
}

void ZbDevice::reportFault(FaultCode code)
{
    // Multistate Input present_value is a ZCL single-precision float.
    float val = static_cast<float>(static_cast<uint8_t>(code));

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(kFaultEp,
        ESP_ZB_ZCL_CLUSTER_ID_MULTI_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_MULTI_INPUT_PRESENT_VALUE_ID,
        &val,
        false);
    esp_zb_lock_release();
}

void ZbDevice::reportSolarData(uint16_t maxChargingPowerW,
                                uint16_t dailyGenerationWh,
                                uint16_t dailyConsumptionWh,
                                uint8_t chargingStatus)
{
    float maxPowerF = static_cast<float>(maxChargingPowerW);
    float genF      = static_cast<float>(dailyGenerationWh);
    float conF      = static_cast<float>(dailyConsumptionWh);
    float statusF   = static_cast<float>(chargingStatus);

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(kMaxChargePowerEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &maxPowerF, false);
    esp_zb_zcl_set_attribute_val(kDailyGenEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &genF, false);
    esp_zb_zcl_set_attribute_val(kDailyConEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &conF, false);
    esp_zb_zcl_set_attribute_val(kChargingStatusEp,
        ESP_ZB_ZCL_CLUSTER_ID_MULTI_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_MULTI_INPUT_PRESENT_VALUE_ID,
        &statusF, false);
    esp_zb_lock_release();
}

// ── Endpoint builders ─────────────────────────────────────────────────────────

void ZbDevice::buildBasicEp(esp_zb_ep_list_t* ep_list)
{
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version   = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source  = 0x04, // DC source (solar)
    };
    esp_zb_identify_cluster_cfg_t identify_cfg = { .identify_time = 0 };

    esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cl,
        esp_zb_basic_cluster_create(&basic_cfg),    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cl,
        esp_zb_identify_cluster_create(&identify_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = 1,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
}

void ZbDevice::buildZoneEps(esp_zb_ep_list_t* ep_list)
{
    for (uint8_t i = ZONE_ID_MIN; i <= ZONE_ID_MAX; ++i) {
        const uint8_t ep = static_cast<uint8_t>(kZoneEpBase + i);

        esp_zb_on_off_cluster_cfg_t on_off_cfg = { .on_off = 0u };

        esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
        esp_zb_cluster_list_add_on_off_cluster(cl,
            esp_zb_on_off_cluster_create(&on_off_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_endpoint_config_t ep_cfg = {
            .endpoint           = ep,
            .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id      = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
            .app_device_version = 0,
        };
        esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
    }
}

void ZbDevice::buildBatteryEp(esp_zb_ep_list_t* ep_list)
{
    esp_zb_power_config_cluster_cfg_t pwr_cfg = {
        .main_voltage          = 0,
        .main_voltage_min_threshold = 0,
    };

    esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_power_config_cluster(cl,
        esp_zb_power_config_cluster_create(&pwr_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = kBatteryEp,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
}

void ZbDevice::buildSolarDataEps(esp_zb_ep_list_t* ep_list)
{
    // Three Analog Input endpoints: max charging power today, daily generation, daily consumption.
    const uint8_t eps[3] = { kMaxChargePowerEp, kDailyGenEp, kDailyConEp };

    for (uint8_t ep : eps) {
        esp_zb_analog_input_cluster_cfg_t ai_cfg = {
            .out_of_service = 0,
            .present_value  = 0.0f,
            .status_flags   = 0,
        };

        esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
        esp_zb_cluster_list_add_analog_input_cluster(cl,
            esp_zb_analog_input_cluster_create(&ai_cfg),
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_endpoint_config_t ep_cfg = {
            .endpoint           = ep,
            .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id      = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
            .app_device_version = 0,
        };
        esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
    }
}

void ZbDevice::buildFaultEp(esp_zb_ep_list_t* ep_list)
{
    // Multistate Input — 8 states matching FaultCode enum (0=None … 7=StaleData)
    esp_zb_multistate_input_cluster_cfg_t ms_cfg = {
        .number_of_states = 8,
        .out_of_service   = 0,
        .present_value    = 0.0f, // FaultCode::None
        .status_flags     = 0,
    };

    esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_multistate_input_cluster(cl,
        esp_zb_multistate_input_cluster_create(&ms_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = kFaultEp,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
}

void ZbDevice::buildChargingStatusEp(esp_zb_ep_list_t* ep_list)
{
    // Multistate Input — 7 states: 0=not started, 1=startup, 2=MPPT,
    // 3=equalisation, 4=boost, 5=float, 6=current limiting
    esp_zb_multistate_input_cluster_cfg_t ms_cfg = {
        .number_of_states = 7,
        .out_of_service   = 0,
        .present_value    = 0.0f,
        .status_flags     = 0,
    };

    esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_multistate_input_cluster(cl,
        esp_zb_multistate_input_cluster_create(&ms_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = kChargingStatusEp,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
}
