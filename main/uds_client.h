#pragma once
#include "esp_err.h"
#include "esp_partition.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief TransferData ilerlemesi her yüzde noktası değiştiğinde çağrılır
 *        (0..100). Çağıran taraf bunu status_hub'a (BLE + web dashboard)
 *        yansıtabilir — CAN/UDS transferi çok yavaş olduğundan
 *        (5 byte/frame + her frame'de ack bekleme) bu geri bildirim
 *        olmadan panel dakikalarca "donmuş" görünür.
 */
typedef void (*uds_progress_cb_t)(int percent);

esp_err_t uds_client_flash_vcu(const esp_partition_t *part, uint32_t fw_size,
                                uds_progress_cb_t progress_cb);
bool      uds_client_is_running(void);
