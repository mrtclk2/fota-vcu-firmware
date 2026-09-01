#pragma once

#include <stddef.h>

/**
 * @brief Durum bildirimlerini tek noktadan yayınlar: hem BLE status
 *        characteristic'ine (ble_notify_status) hem de web dashboard'un
 *        WebSocket yayınına aynı metin gider. Çağıranlar (wifi_handler,
 *        ota_handler) artık ble_notify_status'u doğrudan çağırmaz.
 */
void status_hub_init(void);

void status_hub_publish(const char *text);
void status_hub_get_last(char *buf, size_t size);

/* OTA ilerleme yüzdeleri — üç ayrı aşama, üç ayrı sayaç (aynı göstergeyi
 * paylaşırlarsa "indirme %100" olur olmaz aniden "flash %0"a düşüp
 * kafa karıştırırlar; bu yüzden ayrılar). */
void status_hub_set_self_progress(int percent);        /* Gateway kendi OTA'sı */
void status_hub_set_vcu_download_progress(int percent); /* WiFi ile indirme */
void status_hub_set_vcu_flash_progress(int percent);     /* CAN/UDS ile flash */

void status_hub_get_progress(int *self_pct, int *vcu_dl_pct, int *vcu_flash_pct);
