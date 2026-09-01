#include "wifi_handler.h"
#include "status_hub.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
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

#define NVS_NAMESPACE  "wifi_creds"
#define NVS_KEY_SSID   "ssid"
#define NVS_KEY_PASS   "pass"

/* ── NVS'e kaydet ────────────────────────────────────────────────── */
static void save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, password);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Credentials NVS'e kaydedildi (SSID: %s)", ssid);
}

/* ── NVS'ten oku ve bağlan ───────────────────────────────────────── */
static void load_and_connect(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    char ssid[33] = {0};
    char pass[65] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);

    esp_err_t r1 = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    esp_err_t r2 = nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len);
    nvs_close(h);

    if (r1 != ESP_OK || r2 != ESP_OK || strlen(ssid) == 0) {
        ESP_LOGI(TAG, "NVS'te kayıtlı WiFi yok");
        return;
    }

    ESP_LOGI(TAG, "NVS'ten WiFi yükleniyor: %s", ssid);
    wifi_connect(ssid, pass);
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

    load_and_connect();
}

esp_err_t wifi_connect(const char *ssid, const char *password)
{
    if (wifi_connected || wifi_connecting) {
        ESP_LOGI(TAG, "Farklı ağa geçiliyor, mevcut STA bağlantısı kesiliyor");
        esp_wifi_disconnect();
        wifi_connected  = false;
        wifi_connecting = false;
    }

    save_credentials(ssid, password);

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
