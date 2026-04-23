#include "ble_ota_handler.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "BLE_OTA";

static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *ota_partition = NULL;
static bool ota_running = false;
static size_t total_written = 0;

esp_err_t ble_ota_begin(void)
{
    if (ota_running) {
        ESP_LOGW(TAG, "BLE OTA zaten devam ediyor, sifirlaniyor");
        esp_ota_abort(ota_handle);
        ota_running = false;
    }

    /* Yazılacak OTA partition'ı seç (ota_0 veya ota_1, aktif olmayan) */
    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        ESP_LOGE(TAG, "OTA partition bulunamadi");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA partition: %s (offset: 0x%lx)",
             ota_partition->label, ota_partition->address);

    esp_err_t ret = esp_ota_begin(ota_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    ota_running = true;
    total_written = 0;
    ESP_LOGI(TAG, "BLE OTA baslatildi");
    return ESP_OK;
}

esp_err_t ble_ota_write(const uint8_t *data, size_t len)
{
    if (!ota_running) {
        ESP_LOGE(TAG, "OTA baslatilmadi, once ble_ota_begin cagir");
        return ESP_FAIL;
    }

    if (data == NULL || len == 0) {
        ESP_LOGE(TAG, "Gecersiz veri");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_ota_write(ota_handle, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write basarisiz: %s", esp_err_to_name(ret));
        esp_ota_abort(ota_handle);
        ota_running = false;
        return ret;
    }

    total_written += len;
    ESP_LOGI(TAG, "Yazildi: %u byte (toplam: %u)", (unsigned)len, (unsigned)total_written);
    return ESP_OK;
}

esp_err_t ble_ota_end(void)
{
    if (!ota_running) {
        ESP_LOGE(TAG, "OTA baslatilmadi");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end basarisiz: %s", esp_err_to_name(ret));
        ota_running = false;
        return ret;
    }

    ret = esp_ota_set_boot_partition(ota_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Boot partition ayarlanamadi: %s", esp_err_to_name(ret));
        ota_running = false;
        return ret;
    }

    ESP_LOGI(TAG, "BLE OTA tamamlandi! Toplam: %u byte. Yeniden baslatiliyor...",
             (unsigned)total_written);

    ota_running = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

bool ble_ota_is_running(void)
{
    return ota_running;
}