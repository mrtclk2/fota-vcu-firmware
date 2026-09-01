#include "ota_handler.h"
#include "uds_client.h"
#include "wifi_handler.h"
#include "status_hub.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "OTA";

/* WiFi bağlanana kadar bekle, max 30 saniye */
static esp_err_t wait_for_wifi(void)
{
    const int max_ms      = 30000;
    const int interval_ms = 500;

    for (int elapsed = 0; elapsed < max_ms; elapsed += interval_ms) {
        if (wifi_is_connected()) return ESP_OK;
        ESP_LOGI(TAG, "WiFi bekleniyor... (%d/%d sn)", elapsed / 1000, max_ms / 1000);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }

    ESP_LOGE(TAG, "WiFi bağlantısı zaman aşımına uğradı (30s)");
    return ESP_ERR_TIMEOUT;
}
static volatile bool ota_running = false;
static char ota_url[512];

static void ota_task(void *pvParameter)
{
    if (wait_for_wifi() != ESP_OK) {
        ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA basliyor: %s", ota_url);

    esp_http_client_config_t http_config = {
        .url               = ota_url,
        .timeout_ms        = 15000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    /* ── Gelişmiş OTA: adım adım, doğrulamalı ── */
    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin basarisiz: %s", esp_err_to_name(ret));
        status_hub_publish("OTA: Baslatilamadi");
        ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    /* Chunk chunk indir — bu sayede ilerleme loglanabilir */
    int last_percent = -1;
    while (1) {
        ret = esp_https_ota_perform(ota_handle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int total   = esp_https_ota_get_image_size(ota_handle);
            int written = esp_https_ota_get_image_len_read(ota_handle);
            if (total > 0) {
                int percent = (written * 100) / total;
                ESP_LOGI(TAG, "OTA ilerleme: %d / %d byte (%d%%)",
                         written, total, percent);
                if (percent != last_percent) {
                    last_percent = percent;
                    status_hub_set_self_progress(percent);
                    char status[48];
                    snprintf(status, sizeof(status), "OTA: OTA_PROGRESS (%%%d)", percent);
                    status_hub_publish(status);
                }
            }
            continue;
        }
        break;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA indirme hatasi: %s", esp_err_to_name(ret));
        status_hub_publish("OTA: Indirme hatasi");
        esp_https_ota_abort(ota_handle);
        ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    /* Firmware bütünlüğünü kontrol et */
    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "Firmware eksik indirildi, OTA iptal");
        status_hub_publish("OTA: Eksik indirme, iptal edildi");
        esp_https_ota_abort(ota_handle);
        ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    /* OTA tamamla ve boot partition'ı ayarla */
    ret = esp_https_ota_finish(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish basarisiz: %s", esp_err_to_name(ret));
        status_hub_publish("OTA: Tamamlanamadi");
        ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA basarili! Yeniden baslatiliyor...");
    status_hub_publish("OTA: Basarili, yeniden baslatiliyor");
    ota_running = false;

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    vTaskDelete(NULL);
}

esp_err_t ota_start(const char *url)
{
    if (ota_running) {
        ESP_LOGW(TAG, "OTA zaten devam ediyor");
        return ESP_FAIL;
    }

    if (url == NULL || strlen(url) < 10) {
        ESP_LOGE(TAG, "Gecersiz URL");
        return ESP_FAIL;
    }

    strncpy(ota_url, url, sizeof(ota_url) - 1);
    ota_url[sizeof(ota_url) - 1] = '\0';

    status_hub_set_self_progress(0);
    ota_running = true;

    BaseType_t ret = xTaskCreate(
        ota_task,
        "ota_task",
        8192,   /* OTA için 8KB stack şart */
        NULL,
        5,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "OTA task olusturulamadi");
        ota_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool ota_is_running(void)
{
    return ota_running;
}

/* ══════════════════════════════════════════════════════════════════
 * VCU Firmware: HTTP'den indir → inactive OTA partition'a kaydet
 *               → CAN UDS ile VCU'ya gönder
 * ══════════════════════════════════════════════════════════════════ */
static volatile bool vcu_running = false;
static char          vcu_url[512];

/* CAN/UDS transferi çok yavaş (5 byte/frame + her frame'de ack) — bu
 * geri bildirim olmadan gateway'in kendi paneli dakikalarca "donmuş"
 * görünür, oysa VCU'nun kendi paneli zaten kendi ilerlemesini gösterir. */
static void uds_flash_progress_cb(int percent)
{
    status_hub_set_vcu_flash_progress(percent);
    char status[48];
    snprintf(status, sizeof(status), "OTA: VCU_FLASH_PROGRESS (%%%d)", percent);
    status_hub_publish(status);
}

static void vcu_fw_task(void *pvParameter)
{
    if (wait_for_wifi() != ESP_OK) {
        vcu_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "VCU firmware indiriliyor: %s", vcu_url);

    /* İnaktif OTA partition'ı geçici depo olarak kullan */
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        ESP_LOGE(TAG, "İnaktif OTA partition bulunamadı");
        status_hub_publish("OTA: VCU icin partition bulunamadi");
        goto done;
    }

    if (esp_partition_erase_range(part, 0, part->size) != ESP_OK) {
        ESP_LOGE(TAG, "Partition silinemedi");
        status_hub_publish("OTA: VCU partition silinemedi");
        goto done;
    }

    /* HTTP ile indir, partition'a chunk chunk yaz */
    esp_http_client_config_t http_cfg = {
        .url               = vcu_url,
        .timeout_ms        = 15000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP bağlantısı açılamadı");
        status_hub_publish("OTA: VCU firmware indirme hatasi");
        esp_http_client_cleanup(client);
        goto done;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Content-Length alınamadı");
        status_hub_publish("OTA: VCU firmware indirme hatasi");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        goto done;
    }
    ESP_LOGI(TAG, "VCU firmware boyutu: %d byte", content_length);

    uint8_t  buf[256];
    uint32_t offset = 0;
    int      rlen;
    int      last_percent = -1;

    while (offset < (uint32_t)content_length) {
        rlen = esp_http_client_read(client, (char *)buf, sizeof(buf));
        if (rlen <= 0) break;
        if (esp_partition_write(part, offset, buf, rlen) != ESP_OK) {
            ESP_LOGE(TAG, "Partition yazma hatası");
            break;
        }
        offset += rlen;

        int percent = (int)((offset * 100) / (uint32_t)content_length);
        if (percent != last_percent) {
            last_percent = percent;
            status_hub_set_vcu_download_progress(percent);
            char status[48];
            snprintf(status, sizeof(status), "OTA: VCU_DOWNLOAD_PROGRESS (%%%d)", percent);
            status_hub_publish(status);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (offset != (uint32_t)content_length) {
        ESP_LOGE(TAG, "İndirme eksik: %lu / %d byte", (unsigned long)offset, content_length);
        status_hub_publish("OTA: VCU firmware eksik indirildi");
        goto done;
    }

    ESP_LOGI(TAG, "İndirme tamamlandı. CAN UDS ile VCU'ya gönderiliyor...");
    status_hub_publish("OTA: VCU icin CAN/UDS flash basliyor");

    esp_err_t ret = uds_client_flash_vcu(part, (uint32_t)content_length, uds_flash_progress_cb);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "VCU firmware güncelleme başarılı!");
        status_hub_publish("OTA: VCU firmware guncelleme basarili");
    } else {
        ESP_LOGE(TAG, "VCU UDS flash hatası: %s", esp_err_to_name(ret));
        status_hub_publish("OTA: VCU CAN flash hatasi");
    }

done:
    vcu_running = false;
    vTaskDelete(NULL);
}

esp_err_t vcu_fw_flash(const char *url)
{
    if (vcu_running || ota_running) {
        ESP_LOGW(TAG, "OTA zaten devam ediyor");
        return ESP_FAIL;
    }
    if (!url || strlen(url) < 10) {
        ESP_LOGE(TAG, "Geçersiz URL");
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(vcu_url, url, sizeof(vcu_url) - 1);
    vcu_url[sizeof(vcu_url) - 1] = '\0';
    status_hub_set_vcu_download_progress(0);
    status_hub_set_vcu_flash_progress(0);
    vcu_running = true;

    if (xTaskCreate(vcu_fw_task, "vcu_fw", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "VCU task oluşturulamadı");
        vcu_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool vcu_fw_is_running(void) { return vcu_running; }
