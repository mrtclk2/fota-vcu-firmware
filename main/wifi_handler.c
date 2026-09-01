#include "wifi_handler.h"
#include "status_hub.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "WIFI";

static bool wifi_connected  = false;
static bool wifi_connecting = false;

static char s_sta_ip[16]  = "-";
static char s_ap_ssid[24] = "SECUREGW";

/* Gateway'in kendi yayınladığı yönetim ağı — teknisyen tableti/telefonu
 * aracın WiFi şifresini bilmeden doğrudan buna bağlanıp dashboard'a
 * ulaşabilir. Not: Sabit şifre — üretimde cihaz başına farklılaştırılması
 * önerilir (ör. seri numarasından türetilerek). */
#define GATEWAY_AP_PASSWORD "gateway1234"
#define GATEWAY_AP_CHANNEL  6

/* ── Bilinen ağlar listesi ─────────────────────────────────────────
 * Bench-test cihazı farklı yerlerde (ev/ofis/atölye/telefon hotspot'u)
 * çalışıyor; her seferinde WiFi Ayarla'dan elle seçmek yerine daha önce
 * girilen ağlardan hangisi menzildeyse otomatik ona bağlanır, kopunca
 * sırayla diğerlerini dener. */
#define WIFI_MAX_KNOWN            5
#define WIFI_RETRY_TIMEOUT_MS     8000
#define WIFI_RETRY_CYCLE_DELAY_MS 3000

typedef struct {
    char ssid[33];
    char pass[65];
} known_net_t;

static known_net_t s_known[WIFI_MAX_KNOWN];
static int         s_known_count = 0;

#define NVS_NAMESPACE   "wifi_creds"
#define NVS_KEY_COUNT   "count"
#define NVS_KEY_SSID_FMT "ssid%d"
#define NVS_KEY_PASS_FMT "pass%d"

/* ── Bilinen ağlar listesini NVS'e yaz ──────────────────────────────── */
static void save_known_networks(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_u8(h, NVS_KEY_COUNT, (uint8_t)s_known_count);
    for (int i = 0; i < s_known_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), NVS_KEY_SSID_FMT, i);
        nvs_set_str(h, key, s_known[i].ssid);
        snprintf(key, sizeof(key), NVS_KEY_PASS_FMT, i);
        nvs_set_str(h, key, s_known[i].pass);
    }
    nvs_commit(h);
    nvs_close(h);
}

/* ── Bilinen ağlar listesini NVS'ten oku ────────────────────────────── */
static void load_known_networks(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    uint8_t count = 0;
    nvs_get_u8(h, NVS_KEY_COUNT, &count);
    if (count > WIFI_MAX_KNOWN) count = WIFI_MAX_KNOWN;

    int loaded = 0;
    for (int i = 0; i < count; i++) {
        char key[16];
        size_t ssid_len = sizeof(s_known[loaded].ssid);
        size_t pass_len = sizeof(s_known[loaded].pass);

        snprintf(key, sizeof(key), NVS_KEY_SSID_FMT, i);
        esp_err_t r1 = nvs_get_str(h, key, s_known[loaded].ssid, &ssid_len);
        snprintf(key, sizeof(key), NVS_KEY_PASS_FMT, i);
        esp_err_t r2 = nvs_get_str(h, key, s_known[loaded].pass, &pass_len);

        if (r1 == ESP_OK && r2 == ESP_OK && s_known[loaded].ssid[0] != '\0') {
            loaded++;
        }
    }
    nvs_close(h);

    s_known_count = loaded;
    ESP_LOGI(TAG, "NVS'ten %d bilinen ag yuklendi", s_known_count);
}

/* ── Yeni ağı bilinen listeye ekle/güncelle ve kalıcı yap ───────────── */
static void remember_network(const char *ssid, const char *password)
{
    for (int i = 0; i < s_known_count; i++) {
        if (strcmp(s_known[i].ssid, ssid) == 0) {
            strncpy(s_known[i].pass, password, sizeof(s_known[i].pass) - 1);
            save_known_networks();
            return;
        }
    }

    if (s_known_count >= WIFI_MAX_KNOWN) {
        /* Liste doluysa en eskisini (index 0) at, yerine yenisini ekle */
        memmove(&s_known[0], &s_known[1], sizeof(known_net_t) * (WIFI_MAX_KNOWN - 1));
        s_known_count = WIFI_MAX_KNOWN - 1;
    }

    strncpy(s_known[s_known_count].ssid, ssid, sizeof(s_known[s_known_count].ssid) - 1);
    strncpy(s_known[s_known_count].pass, password, sizeof(s_known[s_known_count].pass) - 1);
    s_known_count++;

    save_known_networks();
    ESP_LOGI(TAG, "Ag bilinenler listesine eklendi: %s (toplam: %d)", ssid, s_known_count);
}

/* ── Event handler ───────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        bool was_connected = wifi_connected;
        wifi_connected  = false;
        wifi_connecting = false;
        strncpy(s_sta_ip, "-", sizeof(s_sta_ip) - 1);
        ESP_LOGI(TAG, "WiFi bağlantısı kesildi");
        if (was_connected) {
            status_hub_publish("Wi-Fi: Bağlantı kesildi");
        } else {
            status_hub_publish("Wi-Fi: Bağlanamadı");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi bağlandı! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected  = true;
        wifi_connecting = false;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));

        char status[64];
        snprintf(status, sizeof(status), "Wi-Fi: WiFi bağlandı. IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));
        status_hub_publish(status);
    }
}

/* ── Otomatik yeniden bağlanma görevi ────────────────────────────────
 * Bağlı değilken bilinen ağları sırayla dener; biri menzilde ve doğruysa
 * bağlanır, kopunca döngü otomatik devam eder. Elle "WiFi Ayarla" ile
 * başlatılan bağlantıyla çakışmaması için wifi_connecting bayrağını da
 * kontrol eder. */
static void wifi_retry_task(void *arg)
{
    int idx = 0;

    while (1) {
        if (wifi_connected || wifi_connecting) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (s_known_count == 0) {
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        known_net_t *net = &s_known[idx % s_known_count];
        ESP_LOGI(TAG, "Otomatik WiFi denemesi (%d/%d): %s",
                 (idx % s_known_count) + 1, s_known_count, net->ssid);

        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid,     net->ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, net->pass, sizeof(wifi_config.sta.password) - 1);

        wifi_connecting = true;
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_connect();

        for (int waited = 0; waited < WIFI_RETRY_TIMEOUT_MS; waited += 300) {
            if (wifi_connected || !wifi_connecting) break;
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        idx++;
        if (!wifi_connected && (idx % s_known_count) == 0) {
            /* Tüm liste bir tur denendi, biraz nefes al */
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_CYCLE_DELAY_MS));
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────── */
void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    /* AP SSID'sini MAC'ten türet: SECUREGW_XXYYZZ (çakışmayı önler) */
    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK) {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "SECUREGW_%02X%02X%02X",
                 mac[3], mac[4], mac[5]);
    }

    wifi_config_t ap_config = {
        .ap = {
            .channel        = GATEWAY_AP_CHANNEL,
            .password       = GATEWAY_AP_PASSWORD,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_config.ap.ssid, s_ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(s_ap_ssid);

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Gateway yönetim ağı yayında: %s", s_ap_ssid);

    load_known_networks();
    xTaskCreate(wifi_retry_task, "wifi_retry", 3072, NULL, 4, NULL);
}

esp_err_t wifi_connect(const char *ssid, const char *password)
{
    if (wifi_connected || wifi_connecting) {
        ESP_LOGI(TAG, "Farklı ağa geçiliyor, mevcut STA bağlantısı kesiliyor");
        esp_wifi_disconnect();
        wifi_connected  = false;
        wifi_connecting = false;
    }

    remember_network(ssid, password);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid,     ssid,     sizeof(wifi_config.sta.ssid)     - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    wifi_connecting = true;
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();

    ESP_LOGI(TAG, "Bağlanılıyor: %s", ssid);
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return wifi_connected;
}

void wifi_get_sta_ip(char *buf, size_t size)
{
    if (size == 0) return;
    strncpy(buf, s_sta_ip, size - 1);
    buf[size - 1] = '\0';
}

void wifi_get_ap_ssid(char *buf, size_t size)
{
    if (size == 0) return;
    strncpy(buf, s_ap_ssid, size - 1);
    buf[size - 1] = '\0';
}

#define WIFI_SCAN_MAX_RAW    20
#define WIFI_SCAN_MAX_UNIQUE 15

esp_err_t wifi_scan_json(char *buf, size_t buf_size)
{
    if (!buf || buf_size < 4) return ESP_ERR_INVALID_ARG;

    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi tarama basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t num = WIFI_SCAN_MAX_RAW;
    static wifi_ap_record_t records[WIFI_SCAN_MAX_RAW];
    esp_wifi_scan_get_ap_records(&num, records);

    /* Aynı SSID'yi birden fazla AP yayınlıyorsa en güçlü sinyali tut */
    wifi_ap_record_t *uniq[WIFI_SCAN_MAX_UNIQUE] = {0};
    int uniq_count = 0;

    for (int i = 0; i < num; i++) {
        if (records[i].ssid[0] == '\0') continue;

        int found = -1;
        for (int j = 0; j < uniq_count; j++) {
            if (strcmp((char *)uniq[j]->ssid, (char *)records[i].ssid) == 0) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            if (records[i].rssi > uniq[found]->rssi) uniq[found] = &records[i];
        } else if (uniq_count < WIFI_SCAN_MAX_UNIQUE) {
            uniq[uniq_count++] = &records[i];
        }
    }

    size_t pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "[");
    for (int i = 0; i < uniq_count; i++) {
        if (pos + 96 >= buf_size) break;
        pos += snprintf(buf + pos, buf_size - pos,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
            i == 0 ? "" : ",",
            (char *)uniq[i]->ssid,
            uniq[i]->rssi,
            uniq[i]->authmode == WIFI_AUTH_OPEN ? "false" : "true");
    }
    snprintf(buf + pos, buf_size - pos, "]");

    return ESP_OK;
}
