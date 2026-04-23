#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief BLE üzerinden gelen OTA chunk'ını işler
 * @param data Gelen veri
 * @param len  Veri uzunluğu
 * @return ESP_OK devam et, ESP_FAIL hata
 */
esp_err_t ble_ota_write(const uint8_t *data, size_t len);

/**
 * @brief BLE OTA işlemini başlatır (ilk chunk gelmeden önce çağrılır)
 */
esp_err_t ble_ota_begin(void);

/**
 * @brief BLE OTA işlemini tamamlar ve yeniden başlatır
 */
esp_err_t ble_ota_end(void);

/**
 * @brief BLE OTA devam ediyor mu?
 */
bool ble_ota_is_running(void);