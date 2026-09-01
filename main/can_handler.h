#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

/* ── TWAI GPIO Tanımları ── */
#define CAN_TX_GPIO  4
#define CAN_RX_GPIO  5

/* ── VCU CAN Mesaj ID'leri ── */
#define CAN_ID_VEHICLE_STATUS   0x100
#define CAN_ID_DRIVE_DATA       0x101
#define CAN_ID_DTC              0x102
#define CAN_ID_HVIL             0x103
#define CAN_ID_VERSION          0x104
#define CAN_ID_SOC              0x105

/* ── UDS CAN ID'leri ── */
#define CAN_ID_UDS_REQ          0x7E0   /* Tester → VCU */
#define CAN_ID_UDS_RESP         0x7E8   /* VCU → Tester */

esp_err_t can_handler_init(void);

/* UDS yanıtlarının yönlendirileceği queue'yu ayarla (NULL = kapat) */
void      can_set_uds_rx_queue(QueueHandle_t queue);

/* UDS isteği gönder (0x7E0 ID üzerinden) */
esp_err_t can_send_uds_frame(const uint8_t *data, uint8_t len);
 