#include "zb/zb_handlers.hpp"
#include "zb/zb_device.hpp"
#include "watering/zone_id.hpp"
#include <esp_log.h>

static const char* TAG = "ZbHandlers";

QueueHandle_t ZbHandlers::cmdQueue_           = nullptr;
uint32_t      ZbHandlers::defaultDurationSec_ = 15u;

void ZbHandlers::init(QueueHandle_t cmdQueue, uint32_t defaultDurationSec)
{
    cmdQueue_           = cmdQueue;
    defaultDurationSec_ = defaultDurationSec;
}

esp_err_t ZbHandlers::onAction(esp_zb_core_action_callback_id_t callback_id,
                                const void* message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        return handleSetAttr(
            static_cast<const esp_zb_zcl_set_attr_value_params_t*>(message));
    default:
        return ESP_OK;
    }
}

esp_err_t ZbHandlers::handleSetAttr(const esp_zb_zcl_set_attr_value_params_t* p)
{
    if (!cmdQueue_) return ESP_OK;

    // Only care about On/Off cluster on zone endpoints (EP 10–14).
    if (p->info.cluster != ESP_ZB_ZCL_CLUSTER_ID_ON_OFF)         return ESP_OK;
    if (p->attribute.id != ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID)     return ESP_OK;

    const uint8_t ep = p->info.dst_endpoint;
    if (ep < ZbDevice::zoneEp(ZoneId::Zone1) ||
        ep > ZbDevice::zoneEp(ZoneId::Zone5)) {
        return ESP_OK;
    }

    // EP 10 → Zone1, EP 14 → Zone5
    const auto zone = static_cast<ZoneId>(ep - ZbDevice::kZoneEpBase);
    const bool on   = (*static_cast<const uint8_t*>(p->attribute.data.value) != 0u);

    ZbWateringCmd cmd;
    cmd.type        = on ? ZbWateringCmd::Type::Request : ZbWateringCmd::Type::Cancel;
    cmd.zone        = zone;
    cmd.durationSec = on ? defaultDurationSec_ : 0u;

    if (xQueueSendToBack(cmdQueue_, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full — zone %u %s dropped",
                 static_cast<unsigned>(zone), on ? "On" : "Off");
    }
    return ESP_OK;
}
