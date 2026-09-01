#pragma once

#include <stdbool.h>
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

/* OTA ilerleme yüzdesi (self-OTA ve VCU-OTA ayrı ayrı takip edilir) */
void status_hub_set_progress(bool is_vcu, int percent);
void status_hub_get_progress(int *self_pct, int *vcu_pct);
