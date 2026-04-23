#include "can_handler.h"
#include "vehicle_data.h"
#include "esp_log.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CAN";

static QueueHandle_t s_uds_rx_queue = NULL;

void can_set_uds_rx_queue(QueueHandle_t queue)
{
    s_uds_rx_queue = queue;
}

esp_err_t can_send_uds_frame(const uint8_t *data, uint8_t len)
{
    if (!data || len == 0 || len > 8) return ESP_ERR_INVALID_ARG;

    twai_message_t msg = {
        .identifier       = CAN_ID_UDS_REQ,
        .data_length_code = len,
        .flags            = TWAI_MSG_FLAG_NONE
    };
    for (uint8_t i = 0; i < len; i++) msg.data[i] = data[i];

    esp_err_t ret = twai_transmit(&msg, pdMS_TO_TICKS(50));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UDS TX hatası: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ────────────────────────────────────────────
 * 0x100 — Tüm araç verisi tek mesajda
 * Byte 0: Mod (State)
 * Byte 1: Vites (0=N, 1=D, 2=R)
 * Byte 2: Gaz % (0-100)
 * Byte 3-4: Tork Nm (int16, big-endian)
 * Byte 5-6: DTC (0x0000 = yok)
 * ──────────────────────────────────────────── */
static void parse_0x100(const twai_message_t *msg, vehicle_data_t *data)
{
    if (msg->data_length_code < 7) {
        ESP_LOGW(TAG, "0x100 mesaji kisa: %d byte", msg->data_length_code);
        return;
    }

    data->state = (vehicle_state_t)msg->data[0];

    switch (msg->data[1]) {
        case 1:  data->gear = GEAR_DRIVE;   break;
        case 2:  data->gear = GEAR_REVERSE; break;
        default: data->gear = GEAR_NEUTRAL; break;
    }

    data->throttle = msg->data[2];

    int16_t raw_tork = (int16_t)((msg->data[3] << 8) | msg->data[4]);
    data->torque = (raw_tork < 0) ? 0 : (uint16_t)raw_tork;

    uint8_t dtc_high = msg->data[5];
    uint8_t dtc_low  = msg->data[6];

    if (dtc_high == 0x00 && dtc_low == 0x00) {
        strncpy(data->dtc, "NONE", sizeof(data->dtc) - 1);
    } else {
        const char prefix[] = {'P', 'C', 'B', 'U'};
        char letter = prefix[(dtc_high >> 6) & 0x03];
        snprintf(data->dtc, sizeof(data->dtc), "%c%01X%02X%02X",
                 letter,
                 (dtc_high >> 4) & 0x03,
                 dtc_high & 0x0F,
                 dtc_low);
    }

    ESP_LOGI(TAG, "VCU | Mod:%d Vites:%d Gaz:%%%d Tork:%dNm DTC:%s",
             data->state, data->gear,
             data->throttle, data->torque,
             data->dtc);
}

/* ────────────────────────────────────────────
 * CAN okuma task
 * ──────────────────────────────────────────── */
static void can_rx_task(void *arg)
{
    vehicle_data_t current = vehicle_data_get();
    twai_message_t msg;

    while (1) {
        esp_err_t ret = twai_receive(&msg, pdMS_TO_TICKS(100));

        if (ret == ESP_OK) {
            if (!msg.rtr) {
                switch (msg.identifier) {
                    case 0x100:
                        parse_0x100(&msg, &current);
                        vehicle_data_update(&current);
                        break;
                    case CAN_ID_UDS_RESP:
                        if (s_uds_rx_queue) {
                            uint8_t item[9] = {0};
                            item[0] = msg.data_length_code;
                            memcpy(&item[1], msg.data, msg.data_length_code);
                            xQueueSend(s_uds_rx_queue, item, 0);
                        }
                        break;
                    default:
                        ESP_LOGI(TAG, "Bilinmeyen ID:0x%03lX DLC:%d Data:%02X %02X %02X %02X %02X %02X %02X %02X",
                            msg.identifier, msg.data_length_code,
                            msg.data_length_code > 0 ? msg.data[0] : 0,
                            msg.data_length_code > 1 ? msg.data[1] : 0,
                            msg.data_length_code > 2 ? msg.data[2] : 0,
                            msg.data_length_code > 3 ? msg.data[3] : 0,
                            msg.data_length_code > 4 ? msg.data[4] : 0,
                            msg.data_length_code > 5 ? msg.data[5] : 0,
                            msg.data_length_code > 6 ? msg.data[6] : 0,
                            msg.data_length_code > 7 ? msg.data[7] : 0);
                        break;
                }
            }
        } else if (ret == ESP_ERR_TIMEOUT) {
            /* Timeout normal */
        } else {
            ESP_LOGE(TAG, "CAN receive hatasi: %s", esp_err_to_name(ret));
        }
    }
}

/* ────────────────────────────────────────────
 * CAN handler başlatma
 * ──────────────────────────────────────────── */
esp_err_t can_handler_init(void)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        CAN_TX_GPIO, CAN_RX_GPIO, 
        TWAI_MODE_NORMAL
    );
    g_config.rx_queue_len = 10;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TWAI driver kurulamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = twai_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TWAI baslatilamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(
        can_rx_task,
        "can_rx",
        4096,
        NULL,
        5,
        NULL
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "CAN RX task olusturulamadi");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CAN handler init tamamlandi (TX:GPIO%d RX:GPIO%d 500Kbps)",
             CAN_TX_GPIO, CAN_RX_GPIO);
    return ESP_OK;
}