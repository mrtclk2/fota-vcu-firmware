#pragma once
#include "esp_err.h"
#include "esp_partition.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t uds_client_flash_vcu(const esp_partition_t *part, uint32_t fw_size);
bool      uds_client_is_running(void);
