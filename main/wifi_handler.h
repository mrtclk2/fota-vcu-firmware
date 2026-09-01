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
