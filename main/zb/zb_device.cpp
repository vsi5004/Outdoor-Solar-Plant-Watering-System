#include "zb/zb_device.hpp"
#include "zb/zb_handlers.hpp"
#include "watering/zone_id.hpp"
#include "aps/esp_zigbee_aps.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_analog_output.h"
#include "zcl/esp_zigbee_zcl_power_config.h"
#include "config.hpp"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <esp_log.h>

static const char* TAG = "ZbDevice";

static volatile bool s_joined = false;
static volatile uint32_t s_joinedAtMs = 0;
static uint8_t s_manufacturerName[33] = {};
static uint8_t s_modelIdentifier[33] = {};
static uint8_t s_swBuildId[33] = {};

// ── Internal helpers ──────────────────────────────────────────────────────────

template <size_t N>
static void encodeZclString(uint8_t (&dst)[33], const char (&src)[N])
{
    static_assert(N <= sizeof(dst), "ZCL strings are limited to 32 characters");
    dst[0] = static_cast<uint8_t>(N - 1u);
    memcpy(dst + 1, src, N - 1u);
}

static void encodeZclString(uint8_t (&dst)[33], const char* src)
{
    const size_t len = strnlen(src, sizeof(dst) - 1u);
    dst[0] = static_cast<uint8_t>(len);
    memcpy(dst + 1, src, len);
}

static void initIdentityStrings()
{
    static bool initialized = false;
    if (initialized) {
        return;
    }

    encodeZclString(s_manufacturerName, config::zigbee::MANUFACTURER_NAME);
    encodeZclString(s_modelIdentifier, config::zigbee::MODEL_IDENTIFIER);
    encodeZclString(s_swBuildId, esp_app_get_description()->version);
    initialized = true;
}

// Overwrite (or create) a reporting config entry in the ZBOSS table.
// Called from configureReporting() (inside the signal handler) to replace any
// stale entries that the previous firmware version may have written to
// zb_storage. Without this, ZBOSS fires stale timers on rejoin and asserts.
// min_interval=30 → floor of 30 s between reports even if the value changes.
// max_interval=90 → heartbeat every 90 s regardless of change.
static void registerAttrReporting(uint8_t ep, uint16_t cluster, uint16_t attrId)
{
    esp_zb_zcl_reporting_info_t info  = {};
    info.direction                    = ESP_ZB_ZCL_REPORT_DIRECTION_SEND;
    info.ep                           = ep;
    info.cluster_id                   = cluster;
    info.cluster_role                 = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
    info.attr_id                      = attrId;
    info.u.send_info.min_interval     = 30;
    info.u.send_info.max_interval     = 90;
    info.u.send_info.delta.u32        = 0;
    info.dst.short_addr               = 0x0000u;  // coordinator
    info.dst.endpoint                 = 1;
    info.dst.profile_id               = ESP_ZB_AF_HA_PROFILE_ID;
    esp_zb_zcl_update_reporting_info(&info);
}

static void registerAnalogPresentValueReporting(uint8_t ep)
{
    registerAttrReporting(ep,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
}

static void sendAttrReport(uint8_t srcEp, uint16_t cluster, uint16_t attrId);

static void reportAnalogPresentValue(uint8_t ep, float value)
{
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(ep,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &value,
        false);
    sendAttrReport(ep,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    esp_zb_lock_release();
}

// Send a one-shot ZCL Report Attributes frame directly to the coordinator.
// Must only be called while the Zigbee lock is held and after network join.
static void sendAttrReport(uint8_t srcEp, uint16_t cluster, uint16_t attrId)
{
    if (!ZbDevice::reportsEnabled()) {
        return;
    }

    esp_zb_zcl_report_attr_cmd_t cmd        = {};
    cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000u;
    cmd.zcl_basic_cmd.dst_endpoint          = 1;
    cmd.zcl_basic_cmd.src_endpoint          = srcEp;
    cmd.address_mode                        = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    cmd.clusterID                           = cluster;
    cmd.direction                           = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    cmd.dis_default_resp                    = 1;
    cmd.attributeID                         = attrId;

    const esp_err_t err = esp_zb_zcl_report_attr_cmd_req(&cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Report attr failed ep=%u cluster=0x%04x attr=0x%04x: %s",
                 srcEp, cluster, attrId, esp_err_to_name(err));
    }
}


// ── Public API ────────────────────────────────────────────────────────────────

void ZbDevice::init()
{
    if (config::zigbee::ERASE_NVRAM_ON_BOOT) {
        ESP_LOGW(TAG, "Erasing Zigbee NVRAM on startup by config request");
        esp_zb_nvram_erase_at_start(true);
    }

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
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    esp_zb_ep_list_t* ep_list = esp_zb_ep_list_create();
    buildBasicEp(ep_list);
    buildZoneEps(ep_list);
    buildBatteryEp(ep_list);
    buildSolarDataEps(ep_list);
    buildZoneWaterTotalEps(ep_list);
    buildFaultEp(ep_list);
    buildChargingStatusEp(ep_list);
    buildClearFaultEp(ep_list);
    esp_zb_device_register(ep_list);

    esp_zb_core_action_handler_register(ZbHandlers::onAction);

    ESP_LOGI(TAG, "Registered EP1, EP10-14, EP20-28, EP31-35, EP41-43");
}

void ZbDevice::configureReporting()
{
    // Called from the Zigbee signal handler immediately after steering succeeds.
    // Runs inside the ZBOSS task context before the scheduler fires, so these
    // update_reporting_info calls overwrite any stale entries that a previous
    // firmware version may have written to zb_storage — preventing the
    // zcl_general_commands.c assert that fires when ZBOSS tries to execute an
    // old reporting config referencing attributes that no longer exist.
    registerAttrReporting(kBatteryEp,
        ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID);
    registerAnalogPresentValueReporting(kMaxChargePowerEp);
    registerAnalogPresentValueReporting(kDailyGenEp);
    registerAnalogPresentValueReporting(kDailyConEp);
    registerAnalogPresentValueReporting(kBatteryVoltEp);
    registerAnalogPresentValueReporting(kPvVoltEp);
    registerAnalogPresentValueReporting(kPvPowerEp);
    registerAnalogPresentValueReporting(kControllerTempEp);
    registerAnalogPresentValueReporting(kWaterLevelEp);
    for (uint8_t i = ZONE_ID_MIN; i <= ZONE_ID_MAX; ++i) {
        registerAnalogPresentValueReporting(static_cast<uint8_t>(kZoneWaterTotalEpBase + i));
    }
    registerAnalogPresentValueReporting(kFaultEp);
    registerAnalogPresentValueReporting(kChargingStatusEp);
    registerAnalogPresentValueReporting(kWatererStateEp);
    registerAnalogPresentValueReporting(kActiveZoneEp);

    s_joined = true;
    s_joinedAtMs = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
    ESP_LOGI(TAG, "Network joined — reporting configured");
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
    sendAttrReport(ep,
        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
    esp_zb_lock_release();
}

void ZbDevice::reportZoneWaterTotal(ZoneId zone, uint64_t totalMilliliters)
{
    reportAnalogPresentValue(zoneWaterTotalEp(zone), static_cast<float>(totalMilliliters) / 1000.0f);
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
    sendAttrReport(kBatteryEp,
        ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID);
    esp_zb_lock_release();
}

void ZbDevice::reportFault(FaultCode code)
{
    // Analog Input present_value is a ZCL single-precision float.
    reportAnalogPresentValue(kFaultEp, static_cast<float>(static_cast<uint8_t>(code)));
}

void ZbDevice::reportSolarData(float batteryVoltageV,
                                float pvVoltageV,
                                uint16_t pvPowerW,
                                float controllerTempC,
                                uint16_t maxChargingPowerW,
                                uint16_t dailyGenerationWh,
                                uint16_t dailyConsumptionWh,
                                uint8_t chargingStatus)
{
    float batteryVoltageF = batteryVoltageV;
    float pvVoltageF      = pvVoltageV;
    float pvPowerF        = static_cast<float>(pvPowerW);
    float controllerTempF = controllerTempC;
    float maxPowerF = static_cast<float>(maxChargingPowerW);
    float genF      = static_cast<float>(dailyGenerationWh);
    float conF      = static_cast<float>(dailyConsumptionWh);
    float statusF   = static_cast<float>(chargingStatus);

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(kBatteryVoltEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &batteryVoltageF, false);
    esp_zb_zcl_set_attribute_val(kPvVoltEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &pvVoltageF, false);
    esp_zb_zcl_set_attribute_val(kPvPowerEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &pvPowerF, false);
    esp_zb_zcl_set_attribute_val(kControllerTempEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &controllerTempF, false);
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
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &statusF, false);
    sendAttrReport(kBatteryVoltEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    sendAttrReport(kPvVoltEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    sendAttrReport(kPvPowerEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    sendAttrReport(kControllerTempEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    sendAttrReport(kMaxChargePowerEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    sendAttrReport(kDailyGenEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    sendAttrReport(kDailyConEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    sendAttrReport(kChargingStatusEp,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    esp_zb_lock_release();
}

void ZbDevice::reportWaterLevel(uint8_t percent)
{
    reportAnalogPresentValue(kWaterLevelEp, static_cast<float>(percent));
}

void ZbDevice::reportWatererState(uint8_t stateCode)
{
    reportAnalogPresentValue(kWatererStateEp, static_cast<float>(stateCode));
}

void ZbDevice::reportActiveZone(uint8_t zoneNumber)
{
    reportAnalogPresentValue(kActiveZoneEp, static_cast<float>(zoneNumber));
}

void ZbDevice::resetClearFaultTrigger()
{
    uint8_t off = 0u;

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(kClearFaultEp,
        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
        &off,
        false);
    sendAttrReport(kClearFaultEp,
        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
    esp_zb_lock_release();
}

bool ZbDevice::isJoined()
{
    return s_joined;
}

bool ZbDevice::reportsEnabled()
{
    if (!s_joined) {
        return false;
    }

    const uint32_t nowMs = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
    return nowMs - s_joinedAtMs >= config::zigbee::REPORT_DELAY_AFTER_JOIN_MS;
}

// ── Endpoint builders ─────────────────────────────────────────────────────────

void ZbDevice::buildBasicEp(esp_zb_ep_list_t* ep_list)
{
    initIdentityStrings();

    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version   = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source  = 0x04, // DC source (solar)
    };
    esp_zb_identify_cluster_cfg_t identify_cfg = { .identify_time = 0 };

    esp_zb_attribute_list_t* basic_attrs = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, s_manufacturerName);
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, s_modelIdentifier);
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, s_swBuildId);

    esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cl,
        basic_attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cl,
        esp_zb_identify_cluster_create(&identify_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = 1,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_COMBINED_INTERFACE_DEVICE_ID,
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

        esp_zb_analog_output_cluster_cfg_t duration_cfg = {
            .out_of_service = 0,
            .present_value  = static_cast<float>(config::pump::DEFAULT_WATERING_DURATION_SEC),
            .status_flags   = 0,
        };
        esp_zb_attribute_list_t* duration_attrs =
            esp_zb_analog_output_cluster_create(&duration_cfg);
        float minDuration = static_cast<float>(config::pump::MIN_WATERING_DURATION_SEC);
        float maxDuration = static_cast<float>(config::pump::MAX_WATERING_DURATION_SEC);
        esp_zb_analog_output_cluster_add_attr(duration_attrs,
            ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_MIN_PRESENT_VALUE_ID, &minDuration);
        esp_zb_analog_output_cluster_add_attr(duration_attrs,
            ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_MAX_PRESENT_VALUE_ID, &maxDuration);
        esp_zb_cluster_list_add_analog_output_cluster(cl,
            duration_attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

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
    // The standard power_config_cluster_cfg_t only covers mains attributes.
    // Battery voltage (0x0020) and percentage (0x0021) must be added explicitly.
    esp_zb_power_config_cluster_cfg_t pwr_cfg = {
        .main_voltage       = 0,
        .main_freq          = 0,
        .main_alarm_mask    = 0,
        .main_voltage_min   = 0,
        .main_voltage_max   = 0,
        .main_voltage_dwell = 0,
    };
    uint8_t initU8 = 0;
    esp_zb_attribute_list_t* pwr_attrs = esp_zb_power_config_cluster_create(&pwr_cfg);
    esp_zb_power_config_cluster_add_attr(pwr_attrs,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &initU8);
    esp_zb_power_config_cluster_add_attr(pwr_attrs,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &initU8);

    esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_power_config_cluster(cl, pwr_attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_analog_input_cluster_cfg_t active_zone_cfg = {
        .out_of_service = 0,
        .present_value  = 0.0f,
        .status_flags   = 0,
    };
    esp_zb_cluster_list_add_analog_input_cluster(cl,
        esp_zb_analog_input_cluster_create(&active_zone_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

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
    const uint8_t eps[] = {
        kMaxChargePowerEp,
        kDailyGenEp,
        kDailyConEp,
        kBatteryVoltEp,
        kPvVoltEp,
        kPvPowerEp,
        kControllerTempEp,
        kWaterLevelEp,
    };

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

void ZbDevice::buildZoneWaterTotalEps(esp_zb_ep_list_t* ep_list)
{
    for (uint8_t i = ZONE_ID_MIN; i <= ZONE_ID_MAX; ++i) {
        const uint8_t ep = static_cast<uint8_t>(kZoneWaterTotalEpBase + i);

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
    // Analog Input encoding FaultCode (0.0 = None … 7.0 = StaleData).
    // Multistate Input was used here previously but ZBOSS asserts when
    // esp_zb_zcl_update_reporting_info is called for that cluster type.
    esp_zb_analog_input_cluster_cfg_t ai_cfg = {
        .out_of_service = 0,
        .present_value  = 0.0f,
        .status_flags   = 0,
    };

    esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_analog_input_cluster(cl,
        esp_zb_analog_input_cluster_create(&ai_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

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
    // Analog Input encoding charging status (0.0=not started … 6.0=current limiting).
    // Same cluster as fault — Multistate Input is not safe to use with reporting.
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
        .endpoint           = kChargingStatusEp,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
}

void ZbDevice::buildClearFaultEp(esp_zb_ep_list_t* ep_list)
{
    esp_zb_on_off_cluster_cfg_t on_off_cfg = { .on_off = 0u };

    esp_zb_cluster_list_t* cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_on_off_cluster(cl,
        esp_zb_on_off_cluster_create(&on_off_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_analog_input_cluster_cfg_t waterer_state_cfg = {
        .out_of_service = 0,
        .present_value  = 0.0f,
        .status_flags   = 0,
    };
    esp_zb_cluster_list_add_analog_input_cluster(cl,
        esp_zb_analog_input_cluster_create(&waterer_state_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = kClearFaultEp,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
}
