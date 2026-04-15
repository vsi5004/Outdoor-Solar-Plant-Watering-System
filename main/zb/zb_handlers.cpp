#include "zb/zb_handlers.hpp"
#include "zb/zb_device.hpp"
#include "watering/zone_id.hpp"
#include "config.hpp"
#include "zcl/esp_zigbee_zcl_analog_output.h"
#include <esp_log.h>

static const char* TAG = "ZbHandlers";

QueueHandle_t ZbHandlers::cmdQueue_           = nullptr;
uint32_t      ZbHandlers::defaultDurationSec_ = config::pump::DEFAULT_WATERING_DURATION_SEC;
uint32_t      ZbHandlers::zoneDurationSec_[ZONE_COUNT] = {};

void ZbHandlers::init(QueueHandle_t cmdQueue, uint32_t defaultDurationSec)
{
    cmdQueue_           = cmdQueue;
    defaultDurationSec_ = clampDurationSec(static_cast<float>(defaultDurationSec));
    for (uint8_t i = 0; i < ZONE_COUNT; ++i) {
        zoneDurationSec_[i] = defaultDurationSec_;
    }
}

esp_err_t ZbHandlers::onAction(esp_zb_core_action_callback_id_t callback_id,
                                const void* message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        return handleSetAttr(
            static_cast<const esp_zb_zcl_set_attr_value_message_t*>(message));
    default:
        return ESP_OK;
    }
}

esp_err_t ZbHandlers::handleSetAttr(const esp_zb_zcl_set_attr_value_message_t* p)
{
    if (!cmdQueue_) return ESP_OK;

    const uint8_t ep = p->info.dst_endpoint;

    if (p->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_OUTPUT) {
        return handleZoneDurationSet(p);
    }

    // Only care about On/Off cluster on zone endpoints (EP 10–14) and EP43.
    if (p->info.cluster != ESP_ZB_ZCL_CLUSTER_ID_ON_OFF)         return ESP_OK;
    if (p->attribute.id != ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID)     return ESP_OK;

    const bool on = (*static_cast<const uint8_t*>(p->attribute.data.value) != 0u);

    if (ep == ZbDevice::kClearFaultEp) {
        if (!on) {
            return ESP_OK;
        }

        const ZbWateringCmd cmd = {
            .type        = ZbWateringCmd::Type::ClearFault,
            .zone        = ZoneId::Zone1,
            .durationSec = 0u,
        };
        if (xQueueSendToBack(cmdQueue_, &cmd, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Command queue full — clear fault dropped");
        }
        return ESP_OK;
    }

    if (ep < ZbDevice::zoneEp(ZoneId::Zone1) ||
        ep > ZbDevice::zoneEp(ZoneId::Zone5)) {
        return ESP_OK;
    }

    // EP 10 → Zone1, EP 14 → Zone5
    const auto zone = static_cast<ZoneId>(ep - ZbDevice::kZoneEpBase);

    ZbWateringCmd cmd;
    cmd.type        = on ? ZbWateringCmd::Type::Request : ZbWateringCmd::Type::Cancel;
    cmd.zone        = zone;
    cmd.durationSec = on ? zoneDurationSec_[zoneIndex(zone)] : 0u;

    if (xQueueSendToBack(cmdQueue_, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full — zone %u %s dropped",
                 static_cast<unsigned>(zone), on ? "On" : "Off");
    }
    return ESP_OK;
}

esp_err_t ZbHandlers::handleZoneDurationSet(const esp_zb_zcl_set_attr_value_message_t* p)
{
    if (p->attribute.id != ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_PRESENT_VALUE_ID) {
        return ESP_OK;
    }

    const uint8_t ep = p->info.dst_endpoint;
    if (ep < ZbDevice::zoneEp(ZoneId::Zone1) ||
        ep > ZbDevice::zoneEp(ZoneId::Zone5)) {
        return ESP_OK;
    }

    if (p->attribute.data.type != ESP_ZB_ZCL_ATTR_TYPE_SINGLE ||
        p->attribute.data.value == nullptr) {
        ESP_LOGW(TAG, "Ignored duration write on EP%u with unexpected type 0x%02x",
                 ep,
                 static_cast<unsigned>(p->attribute.data.type));
        return ESP_OK;
    }

    const auto zone = static_cast<ZoneId>(ep - ZbDevice::kZoneEpBase);
    const float requested = *static_cast<const float*>(p->attribute.data.value);
    const uint32_t durationSec = clampDurationSec(requested);
    zoneDurationSec_[zoneIndex(zone)] = durationSec;
    ESP_LOGI(TAG, "Zone %u duration set to %lu s",
             static_cast<unsigned>(zone),
             static_cast<unsigned long>(durationSec));
    return ESP_OK;
}

uint32_t ZbHandlers::clampDurationSec(float requestedSec)
{
    constexpr float minDuration = static_cast<float>(config::pump::MIN_WATERING_DURATION_SEC);
    constexpr float maxDuration = static_cast<float>(config::pump::MAX_WATERING_DURATION_SEC);

    if (!(requestedSec >= minDuration)) {
        return config::pump::MIN_WATERING_DURATION_SEC;
    }
    if (requestedSec > maxDuration) {
        return config::pump::MAX_WATERING_DURATION_SEC;
    }
    return static_cast<uint32_t>(requestedSec + 0.5f);
}
