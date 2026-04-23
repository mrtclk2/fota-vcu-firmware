#pragma once

#include "esp_err.h"
#include <stdbool.h>

void      wifi_init(void);
esp_err_t wifi_connect(const char *ssid, const char *password);
bool      wifi_is_connected(void);
