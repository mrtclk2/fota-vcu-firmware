#include "ble_handler.h"
#include "wifi_handler.h"
#include "ota_handler.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "BLE";
static const char *DEVICE_NAME = "FOTA_ESP32";

/* ── UUID'ler ── */
static const ble_uuid128_t wifi_chr_uuid =
    BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15,
                     0xde, 0xef, 0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

static const ble_uuid128_t ota_chr_uuid =
    BLE_UUID128_INIT(0x24, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15,
                     0xde, 0xef, 0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

static const ble_uuid128_t vehicle_chr_uuid =
    BLE_UUID128_INIT(0x25, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15,
                     0xde, 0xef, 0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

static const ble_uuid128_t status_chr_uuid =
    BLE_UUID128_INIT(0x26, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15,
                     0xde, 0xef, 0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

static uint16_t wifi_chr_val_handle;
static uint16_t ota_chr_val_handle;
static uint16_t vehicle_chr_val_handle;
static uint16_t status_chr_val_handle;

/* Bağlı client'ın connection handle'ı */
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

/* ── WiFi RX buffer ── */
static char rx_buffer[256];
static int  rx_len = 0;

static void reset_rx_buffer(void)
{
    rx_len = 0;
    rx_buffer[0] = '\0';
}

/* ────────────────────────────────────────────
 * WiFi characteristic: WIFI:ssid,password
 * ──────────────────────────────────────────── */
static int wifi_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return 0;
    }

    char chunk[128] = {0};
    int len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > (int)sizeof(chunk) - 1) {
        len = (int)sizeof(chunk) - 1;
    }

    ble_hs_mbuf_to_flat(ctxt->om, chunk, len, NULL);
    chunk[len] = '\0';

    ESP_LOGI(TAG, "WiFi chunk alindi: %s", chunk);

    if (rx_len + len >= (int)sizeof(rx_buffer) - 1) {
        ESP_LOGE(TAG, "Buffer overflow, temizleniyor");
        reset_rx_buffer();
        return 0;
    }

    memcpy(rx_buffer + rx_len, chunk, len);
    rx_len += len;
    rx_buffer[rx_len] = '\0';

    if (strncmp(rx_buffer, "WIFI:", 5) != 0) {
        ESP_LOGE(TAG, "Gecersiz baslik, buffer temizleniyor");
        reset_rx_buffer();
        return 0;
    }

    char *payload = rx_buffer + 5;
    char *comma   = strchr(payload, ',');

    if (comma == NULL) {
        return 0;
    }

    if (strlen(comma + 1) < 8) {
        return 0;
    }

    *comma = '\0';
    char *ssid = payload;
    char *pass = comma + 1;

    ESP_LOGI(TAG, "SSID: %s", ssid);
    ESP_LOGI(TAG, "PASS: %s", pass);

    wifi_connect(ssid, pass);
    reset_rx_buffer();
    return 0;
}

/* ────────────────────────────────────────────
 * OTA characteristic
 * Komutlar:
 *   "OTA:URL:http://192.168.1.10:8080/firmware.bin"  → WiFi ile indir
 *   "OTA:STATUS"                                      → durum bilgisi döner
 * ──────────────────────────────────────────── */
static int ota_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return 0;
    }

    int len = OS_MBUF_PKTLEN(ctxt->om);
    if (len <= 0 || len > 511) {
        return 0;
    }

    char buf[512] = {0};
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    buf[len] = '\0';

    ESP_LOGI(TAG, "OTA komutu alindi: %s", buf);

    /* OTA:URL:<url> → WiFi üzerinden backend'den indir */
    if (strncmp(buf, "OTA:URL:", 8) == 0) {
        const char *url = buf + 8;
        if (strlen(url) < 10) {
            ESP_LOGE(TAG, "Gecersiz URL");
            return 0;
        }
        ESP_LOGI(TAG, "OTA baslatiliyor, URL: %s", url);
        esp_err_t ret = ota_start(url);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA baslatilamadi: %s", esp_err_to_name(ret));
        }
        return 0;
    }

    /* OTA:VCU:URL:<url> → VCU firmware'ini HTTP'den çek, CAN UDS ile gönder */
    if (strncmp(buf, "OTA:VCU:URL:", 12) == 0) {
        const char *url = buf + 12;
        if (strlen(url) < 10) {
            ESP_LOGE(TAG, "Geçersiz VCU URL");
            return 0;
        }
        ESP_LOGI(TAG, "VCU OTA başlatılıyor, URL: %s", url);
        esp_err_t ret = vcu_fw_flash(url);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "VCU OTA başlatılamadı: %s", esp_err_to_name(ret));
        }
        return 0;
    }

    /* OTA:STATUS → tüm OTA durumları */
    if (strncmp(buf, "OTA:STATUS", 10) == 0) {
        char status[96];
        snprintf(status, sizeof(status), "OTA: Self=%s VCU=%s",
                 ota_is_running()     ? "RUNNING" : "IDLE",
                 vcu_fw_is_running()  ? "RUNNING" : "IDLE");
        ESP_LOGI(TAG, "%s", status);
        ble_notify_status(status);
        return 0;
    }

    ESP_LOGW(TAG, "Bilinmeyen OTA komutu: %s", buf);
    return 0;
}

/* ────────────────────────────────────────────
 * Vehicle Data characteristic (NOTIFY only)
 * ──────────────────────────────────────────── */
static int vehicle_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    (void)ctxt;
    /* Sadece NOTIFY — write desteklenmiyor */
    return 0;
}

/* ────────────────────────────────────────────
 * Status characteristic (NOTIFY only)
 * WiFi bağlantısı ve OTA/VCU flash olaylarının
 * insan-okunur durum metinleri buradan gönderilir.
 * ──────────────────────────────────────────── */
static int status_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    (void)ctxt;
    /* Sadece NOTIFY — write desteklenmiyor */
    return 0;
}

/* ── GATT servis tablosu ── */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1815),
        .characteristics = (struct ble_gatt_chr_def[]) {
            /* WiFi credential characteristic */
            {
                .uuid       = &wifi_chr_uuid.u,
                .access_cb  = wifi_chr_access,
                .flags      = BLE_GATT_CHR_F_WRITE,
                .val_handle = &wifi_chr_val_handle,
            },
            /* OTA characteristic */
            {
                .uuid       = &ota_chr_uuid.u,
                .access_cb  = ota_chr_access,
                .flags      = BLE_GATT_CHR_F_WRITE,
                .val_handle = &ota_chr_val_handle,
            },
            /* Vehicle Data characteristic */
            {
                .uuid       = &vehicle_chr_uuid.u,
                .access_cb  = vehicle_chr_access,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &vehicle_chr_val_handle,
            },
            /* Status characteristic (WiFi/OTA olay metinleri) */
            {
                .uuid       = &status_chr_uuid.u,
                .access_cb  = status_chr_access,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &status_chr_val_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

/* ── GAP event handler — bağlantı takibi ── */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "BLE baglandi, conn_handle: %d", s_conn_handle);
            } else {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                ESP_LOGE(TAG, "BLE baglanti basarisiz");
                /* Tekrar advertise et */
                ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,
                                  &(struct ble_gap_adv_params){
                                      .conn_mode = BLE_GAP_CONN_MODE_UND,
                                      .disc_mode = BLE_GAP_DISC_MODE_GEN
                                  }, ble_gap_event, NULL);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI(TAG, "BLE baglanti kesildi");
            /* Tekrar advertise et */
            ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,
                              &(struct ble_gap_adv_params){
                                  .conn_mode = BLE_GAP_CONN_MODE_UND,
                                  .disc_mode = BLE_GAP_DISC_MODE_GEN
                              }, ble_gap_event, NULL);
            break;

        default:
            break;
    }
    return 0;
}

/* ── Vehicle Data notify fonksiyonu ── */
void ble_notify_vehicle_data(const char *json)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return; /* Bağlı client yok */
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(json, strlen(json));
    if (om == NULL) {
        ESP_LOGE(TAG, "mbuf olusturulamadi");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, vehicle_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Notify gonderilemedi: %d", rc);
    }
}

/* ── Status notify fonksiyonu ── */
void ble_notify_status(const char *text)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return; /* Bağlı client yok */
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(text, strlen(text));
    if (om == NULL) {
        ESP_LOGE(TAG, "status mbuf olusturulamadi");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, status_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Status notify gonderilemedi: %d", rc);
    }
}

static void ble_on_sync(void)
{
    ble_addr_t addr;
    ble_hs_id_gen_rnd(1, &addr);
    ble_hs_id_set_rnd(addr.val);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {0};
    fields.flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name             = (uint8_t *)DEVICE_NAME;
    fields.name_len         = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,
                      &adv_params, ble_gap_event, NULL);

    ESP_LOGI(TAG, "BLE advertising basladi: %s", DEVICE_NAME);
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_init(void)
{
    nimble_port_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE init tamamlandi");
}