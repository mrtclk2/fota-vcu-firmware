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