#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ble_handler.h"
#include "wifi_handler.h"
#include "vehicle_data.h"
#include "can_handler.h"
#include "status_hub.h"
#include "gateway_web_server.h"

void app_main(void) {
    /* NVS başlat - WiFi ve BLE için gerekli */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* ── OTA rollback koruması ──
     * Eğer bir önceki OTA sonrası ilk boot ise firmware'i geçerli işaretle.
     * Bu olmadan watchdog tetiklenince otomatik rollback yapar. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI("MAIN", "OTA sonrasi ilk boot - firmware dogrulaniyor");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    status_hub_init();

    wifi_init();
    ble_init();

    /* Araç veri modülünü başlat (periyodik BLE notify task) */
    vehicle_data_init();

    can_handler_init();

    /* Yönetim paneli: WiFi AP + HTTP/WebSocket (Core 0) */
    xTaskCreatePinnedToCore(gateway_web_server_start_task, "gw_web", 8192, NULL, 3, NULL, 0);
}