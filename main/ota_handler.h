#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Kendi firmware'ini HTTP'den çekip günceller */
esp_err_t ota_start(const char *url);
bool      ota_is_running(void);

/* VCU firmware'ini HTTP'den çekip CAN UDS ile VCU'ya gönderir */
esp_err_t vcu_fw_flash(const char *url);
bool      vcu_fw_is_running(void);
 