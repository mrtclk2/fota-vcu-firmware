#include "uds_client.h"
#include "can_handler.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "UDS_CLIENT";

#define UDS_TIMEOUT_MS           500
#define UDS_DATA_PER_FRAME       5      /* maxBlockLen=7: SID(1)+seq(1)+data(5) */
#define SECURITY_SEED_MASK       0xA5A5A5A5UL

static volatile bool  s_running  = false;
static QueueHandle_t  s_rx_queue = NULL;

/* ── Yanıt bekle ─────────────────────────────────────────────────── */
static int uds_recv(uint8_t *buf, uint8_t buf_size)
{
    uint8_t item[9] = {0};
    if (xQueueReceive(s_rx_queue, item, pdMS_TO_TICKS(UDS_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Yanıt zaman aşımı");
        return -1;
    }
    uint8_t len = item[0];
    if (len > buf_size) len = buf_size;
    memcpy(buf, &item[1], len);
    return len;
}

/* ── Gönder + yanıt bekle ────────────────────────────────────────── */
static esp_err_t uds_req(const uint8_t *req, uint8_t req_len,
                         uint8_t *resp_out, uint8_t *resp_len_out,
                         uint8_t expected_pos_sid)
{
    esp_err_t ret = can_send_uds_frame(req, req_len);
    if (ret != ESP_OK) return ret;

    uint8_t buf[8];
    int len = uds_recv(buf, sizeof(buf));
    if (len < 0) return ESP_ERR_TIMEOUT;

    if (buf[0] == 0x7F) {
        ESP_LOGE(TAG, "NRC → SID:0x%02X NRC:0x%02X", buf[1], buf[2]);
        return ESP_FAIL;
    }
    if (buf[0] != expected_pos_sid) {
        ESP_LOGE(TAG, "Beklenmeyen yanıt 0x%02X (beklenen 0x%02X)", buf[0], expected_pos_sid);
        return ESP_FAIL;
    }
    if (resp_out && resp_len_out) {
        *resp_len_out = (uint8_t)len;
        memcpy(resp_out, buf, len);
    }
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────── */
esp_err_t uds_client_flash_vcu(const esp_partition_t *part, uint32_t fw_size)
{
    if (s_running) return ESP_ERR_INVALID_STATE;
    s_running = true;

    s_rx_queue = xQueueCreate(16, 9);
    if (!s_rx_queue) { s_running = false; return ESP_ERR_NO_MEM; }
    can_set_uds_rx_queue(s_rx_queue);

    esp_err_t ret;
    uint8_t req[8], resp[8], resp_len;

    /* 1. DiagnosticSessionControl → Programming Session */
    ESP_LOGI(TAG, "[1/5] Programming Session...");
    req[0] = 0x10; req[1] = 0x02;
    ret = uds_req(req, 2, NULL, NULL, 0x50);
    if (ret != ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 2. SecurityAccess — seed iste */
    ESP_LOGI(TAG, "[2/5] SecurityAccess: seed...");
    req[0] = 0x27; req[1] = 0x01;
    ret = uds_req(req, 2, resp, &resp_len, 0x67);
    if (ret != ESP_OK || resp_len < 6) { ret = ESP_FAIL; goto cleanup; }

    /* 3. SecurityAccess — key gönder */
    {
        uint32_t seed = ((uint32_t)resp[2] << 24) | ((uint32_t)resp[3] << 16) |
                        ((uint32_t)resp[4] <<  8) |  (uint32_t)resp[5];
        uint32_t key  = seed ^ SECURITY_SEED_MASK;
        ESP_LOGI(TAG, "   seed=0x%08lX → key=0x%08lX", (unsigned long)seed, (unsigned long)key);
        req[0] = 0x27; req[1] = 0x02;
        req[2] = (key >> 24) & 0xFF; req[3] = (key >> 16) & 0xFF;
        req[4] = (key >>  8) & 0xFF; req[5] =  key        & 0xFF;
        ret = uds_req(req, 6, NULL, NULL, 0x67);
        if (ret != ESP_OK) goto cleanup;
    }

    /* 4. RequestDownload */
    ESP_LOGI(TAG, "[3/5] RequestDownload: %lu byte", (unsigned long)fw_size);
    req[0] = 0x34; req[1] = 0x00; req[2] = 0x44;
    req[3] = (fw_size >> 24) & 0xFF; req[4] = (fw_size >> 16) & 0xFF;
    req[5] = (fw_size >>  8) & 0xFF; req[6] =  fw_size        & 0xFF;
    ret = uds_req(req, 7, NULL, NULL, 0x74);
    if (ret != ESP_OK) goto cleanup;

    /* 5. TransferData — partition'dan oku, CAN'a gönder */
    ESP_LOGI(TAG, "[4/5] TransferData...");
    {
        uint8_t  block_seq = 1;
        uint32_t offset    = 0;
        uint8_t  chunk[UDS_DATA_PER_FRAME];

        while (offset < fw_size) {
            uint32_t rem      = fw_size - offset;
            uint8_t  data_len = (rem > UDS_DATA_PER_FRAME) ? UDS_DATA_PER_FRAME : (uint8_t)rem;

            ret = esp_partition_read(part, offset, chunk, data_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Partition okuma hatası offset=%lu", (unsigned long)offset);
                goto cleanup;
            }

            req[0] = 0x36;
            req[1] = block_seq;
            memcpy(&req[2], chunk, data_len);

            ret = uds_req(req, 2 + data_len, NULL, NULL, 0x76);
            if (ret != ESP_OK) goto cleanup;

            offset    += data_len;
            block_seq  = (block_seq == 0xFF) ? 1 : (block_seq + 1);

            if ((offset / UDS_DATA_PER_FRAME) % 2000 == 0) {
                ESP_LOGI(TAG, "   %lu / %lu byte (%lu%%)",
                         (unsigned long)offset, (unsigned long)fw_size,
                         (unsigned long)(offset * 100 / fw_size));
            }
        }
    }

    /* 6. RequestTransferExit */
    ESP_LOGI(TAG, "[5/5] TransferExit...");
    req[0] = 0x37;
    ret = uds_req(req, 1, NULL, NULL, 0x77);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "VCU firmware başarıyla gönderildi (%lu byte)", (unsigned long)fw_size);
    }

cleanup:
    can_set_uds_rx_queue(NULL);
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
    s_running  = false;
    return ret;
}

bool uds_client_is_running(void) { return s_running; }
