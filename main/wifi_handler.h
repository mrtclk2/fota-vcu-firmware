#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

void      wifi_init(void);
esp_err_t wifi_connect(const char *ssid, const char *password);
bool      wifi_is_connected(void);

/* STA IP adresini yazar ("-" eğer bağlı değilse) */
void wifi_get_sta_ip(char *buf, size_t size);

/* Gateway'in kendi yönetim AP'sinin SSID'sini yazar (ör. "SECUREGW_AB12CD") */
void wifi_get_ap_ssid(char *buf, size_t size);

/* Bağlanılabilecek WiFi ağlarını tarar (~1-3 sn bloklar), sonucu tek bir
 * JSON dizisi olarak buf'a yazar: [{"ssid":"...","rssi":-52,"secure":true},...]
 * Aynı SSID birden fazla AP'den görünüyorsa en güçlü sinyal tutulur. */
esp_err_t wifi_scan_json(char *buf, size_t buf_size);
