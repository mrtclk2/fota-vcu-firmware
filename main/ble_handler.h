#pragma once

#include "esp_err.h"

/**
 * @brief BLE stack'i başlatır, advertising başlatır
 */
void ble_init(void);

/**
 * @brief Araç verisini bağlı mobil uygulamaya BLE NOTIFY ile gönderir
 * @param json JSON formatında araç verisi string'i
 */
void ble_notify_vehicle_data(const char *json);

/**
 * @brief WiFi/OTA gibi olayların durum metnini bağlı mobil uygulamaya
 *        BLE NOTIFY ile gönderir (örn. "OTA: OTA_PROGRESS (%20)")
 * @param text Durum metni (UTF-8, en fazla ~150 byte)
 */
void ble_notify_status(const char *text);