#include "zb/zb_handlers.hpp"
#include "zb/zb_device.hpp"
#include "watering/watering_request.hpp"
#include "watering/zone_id.hpp"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "ZbHandlers";

WateringFsm*     ZbHandlers::fsm_                = nullptr;
SemaphoreHandle_t ZbHandlers::mutex_             = nullptr;
uint32_t         ZbHandlers::defaultDurationSec_ = 15u;

// ── Public API ────────────────────────────────────────────────────────────────

void ZbHandlers::setFsm(WateringFsm* fsm, SemaphoreHandle_t mutex,
                         uint32_t defaultDurationSec)
{
    fsm_                = fsm;
    mutex_              = mutex;
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

// ── Private helpers ───────────────────────────────────────────────────────────

esp_err_t ZbHandlers::handleSetAttr(const esp_zb_zcl_set_attr_value_params_t* p)
{
    if (!fsm_) return ESP_OK;

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

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (on) {
        const uint32_t nowMs = pdTICKS_TO_MS(xTaskGetTickCount());
        const WateringRequest req{ zone, defaultDurationSec_, WaterSource::HaManual };
        if (!fsm_->request(req, nowMs)) {
            ESP_LOGW(TAG, "Zone %u request rejected — fault %u",
                     static_cast<unsigned>(zone),
                     static_cast<unsigned>(fsm_->getLastFault()));
        }
    } else {
        fsm_->cancel();
        ESP_LOGI(TAG, "Zone %u cancelled via Zigbee Off",
                 static_cast<unsigned>(zone));
    }
    xSemaphoreGive(mutex_);
    return ESP_OK;
}
