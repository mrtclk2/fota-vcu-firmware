#include "vcu_releases.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

static const char *TAG = "VCU_RELEASES";

#define GITHUB_OWNER "mrtclk2"
#define GITHUB_REPO  "vcu_kodlar"
#define GITHUB_API_URL \
    "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases?per_page=5"

/* GitHub API yanıtı bu kadar byte'ı geçmez varsayımıyla sabit tampon;
 * taşarsa okuma orada kesilir, cJSON yine de geçerli bir prefix bulamazsa
 * parse hata döner (aşağıda ele alınıyor). */
#define GITHUB_RESP_BUF_SIZE 16384

static esp_err_t fetch_raw(char **out_raw)
{
    esp_http_client_config_t cfg = {
        .url               = GITHUB_API_URL,
        .timeout_ms        = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "User-Agent", "SecureGateway-ESP32");
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Baglanti acilamadi: %s", esp_err_to_name(ret));
        esp_http_client_cleanup(client);
        return ret;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "GitHub API HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char *raw = malloc(GITHUB_RESP_BUF_SIZE);
    if (!raw) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int off = 0, r;
    while (off < GITHUB_RESP_BUF_SIZE - 1) {
        r = esp_http_client_read(client, raw + off, GITHUB_RESP_BUF_SIZE - 1 - off);
        if (r <= 0) break;
        off += r;
    }
    raw[off] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    *out_raw = raw;
    return ESP_OK;
}

esp_err_t vcu_releases_fetch_json(char *buf, size_t buf_size)
{
    if (!buf || buf_size < 4) return ESP_ERR_INVALID_ARG;

    char *raw = NULL;
    esp_err_t ret = fetch_raw(&raw);
    if (ret != ESP_OK) return ret;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "GitHub yaniti parse edilemedi");
        if (root) cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "[");
    bool first = true;

    int release_count = cJSON_GetArraySize(root);
    for (int i = 0; i < release_count; i++) {
        cJSON *rel = cJSON_GetArrayItem(root, i);
        cJSON *tag = cJSON_GetObjectItem(rel, "tag_name");
        cJSON *assets = cJSON_GetObjectItem(rel, "assets");
        if (!cJSON_IsArray(assets)) continue;

        int asset_count = cJSON_GetArraySize(assets);
        for (int j = 0; j < asset_count; j++) {
            cJSON *asset = cJSON_GetArrayItem(assets, j);
            cJSON *name  = cJSON_GetObjectItem(asset, "name");
            cJSON *size  = cJSON_GetObjectItem(asset, "size");
            cJSON *url   = cJSON_GetObjectItem(asset, "browser_download_url");

            if (!cJSON_IsString(name) || !strstr(name->valuestring, ".bin")) continue;
            if (!cJSON_IsString(url)) continue;
            if (pos + 320 >= buf_size) goto done;

            pos += snprintf(buf + pos, buf_size - pos,
                "%s{\"tag\":\"%s\",\"name\":\"%s\",\"size\":%d,\"url\":\"%s\"}",
                first ? "" : ",",
                cJSON_IsString(tag) ? tag->valuestring : "?",
                name->valuestring,
                cJSON_IsNumber(size) ? size->valueint : 0,
                url->valuestring);
            first = false;
        }
    }

done:
    pos += snprintf(buf + pos, buf_size - pos, "]");
    cJSON_Delete(root);
    return ESP_OK;
}
