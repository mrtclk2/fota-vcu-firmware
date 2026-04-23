#include "vehicle_data.h"
#include "ble_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
 
static const char *TAG = "VEHICLE";
 
/* Periyodik gönderim aralığı (ms) */
#define VEHICLE_NOTIFY_PERIOD_MS  500
 
/* Mevcut araç verisi */
static vehicle_data_t s_data = {
    .soc         = 0,
    .state       = VEHICLE_STATE_STANDBY,
    .gear        = GEAR_NEUTRAL,
    .torque      = 0,
    .throttle    = 0,
    .brake       = false,
    .dtc         = "NONE",
    .hvil_alarm  = false,
    .fw_version  = "v1.0.0"
};
 
/* Thread-safety için mutex */
static SemaphoreHandle_t s_mutex = NULL;
 
/* Hemen bildirim gönderilmesi gereken flag */
static bool s_notify_now = false;
 
/* ── JSON üretici ── */
void vehicle_data_to_json(char *buf, size_t size)
{
    const char *gear_str;
    switch (s_data.gear) {
        case GEAR_DRIVE:   gear_str = "D"; break;
        case GEAR_REVERSE: gear_str = "R"; break;
        default:           gear_str = "N"; break;
    }
 
    snprintf(buf, size,
        "{"
        "\"soc\":%u,"
        "\"state\":%u,"
        "\"gear\":\"%s\","
        "\"torque\":%u,"
        "\"throttle\":%u,"
        "\"brake\":%s,"
        "\"dtc\":\"%s\","
        "\"hvil\":\"%s\","
        "\"fw_ver\":\"%s\""
        "}",
        s_data.soc,
        (uint8_t)s_data.state,
        gear_str,
        s_data.torque,
        s_data.throttle,
        s_data.brake ? "true" : "false",
        s_data.dtc,
        s_data.hvil_alarm ? "ALARM" : "OK",
        s_data.fw_version
    );
}
 
/* ── Kritik alan değişti mi? ── */
static bool is_critical_change(const vehicle_data_t *old_data,
                                const vehicle_data_t *new_data)
{
    if (old_data->state      != new_data->state)      return true;
    if (old_data->hvil_alarm != new_data->hvil_alarm) return true;
    if (strcmp(old_data->dtc, new_data->dtc) != 0)    return true;
    if (old_data->gear       != new_data->gear)       return true;
    return false;
}
 
/* ── Periyodik notify task ── */
static void vehicle_notify_task(void *arg)
{
    char json_buf[256];

    while (1) {
        /* Kritik değişiklik varsa 50ms, yoksa 500ms bekle */
        TickType_t wait = s_notify_now
                          ? pdMS_TO_TICKS(50)
                          : pdMS_TO_TICKS(VEHICLE_NOTIFY_PERIOD_MS);
        vTaskDelay(wait);

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            vehicle_data_to_json(json_buf, sizeof(json_buf));
            s_notify_now = false;   /* ← flag burada sıfırlanıyor */
            xSemaphoreGive(s_mutex);
        } else {
            continue;
        }

        ble_notify_vehicle_data(json_buf);
    }
}
 
/* ── Başlatma ── */
esp_err_t vehicle_data_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex olusturulamadi");
        return ESP_FAIL;
    }
 
    BaseType_t ret = xTaskCreate(
        vehicle_notify_task,
        "vehicle_notify",
        4096,
        NULL,
        4,
        NULL
    );
 
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task olusturulamadi");
        return ESP_FAIL;
    }
 
    ESP_LOGI(TAG, "Vehicle data init tamamlandi");
    return ESP_OK;
}
 
/* ── Veri güncelleme ── */
void vehicle_data_update(const vehicle_data_t *new_data)
{
    if (new_data == NULL) return;
 
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        /* Kritik değişiklik varsa hemen notify iste */
        if (is_critical_change(&s_data, new_data)) {
            s_notify_now = true;
            ESP_LOGI(TAG, "Kritik degisiklik - aninda notify");
        }
        memcpy(&s_data, new_data, sizeof(vehicle_data_t));
        xSemaphoreGive(s_mutex);
    }
}
 
/* ── Veri okuma ── */
vehicle_data_t vehicle_data_get(void)
{
    vehicle_data_t copy;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(&copy, &s_data, sizeof(vehicle_data_t));
        xSemaphoreGive(s_mutex);
    }
    return copy;
}
 