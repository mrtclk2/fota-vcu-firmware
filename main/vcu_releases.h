#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * @brief GitHub'daki vcu_kodlar deposunun release'lerini çeker, .bin uzantılı
 *        her asset'i tek bir JSON dizisi olarak buf'a yazar:
 *        [{"tag":"v1.0.1","name":"vcu_firmware.bin","size":123456,
 *          "url":"https://github.com/.../vcu_firmware.bin"}, ...]
 *
 *        Internet (STA) bağlantısı gerektirir. Kimlik doğrulama kullanmaz
 *        (public repo + GitHub'ın anonim rate limiti: 60 istek/saat yeterli).
 */
esp_err_t vcu_releases_fetch_json(char *buf, size_t buf_size);
