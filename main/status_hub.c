#include "status_hub.h"
#include "ble_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static char s_last[128] = "Henuz durum bildirimi yok";
static SemaphoreHandle_t s_mutex;

static volatile int s_self_pct = 0;
static volatile int s_vcu_pct = 0;

void status_hub_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void status_hub_publish(const char *text)
{
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_last, text, sizeof(s_last) - 1);
        s_last[sizeof(s_last) - 1] = '\0';
        xSemaphoreGive(s_mutex);
    }
    ble_notify_status(text);
}

void status_hub_get_last(char *buf, size_t size)
{
    if (size == 0) return;
    buf[0] = '\0';
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(buf, s_last, size - 1);
        buf[size - 1] = '\0';
        xSemaphoreGive(s_mutex);
    }
}

void status_hub_set_progress(bool is_vcu, int percent)
{
    if (is_vcu) {
        s_vcu_pct = percent;
    } else {
        s_self_pct = percent;
    }
}

void status_hub_get_progress(int *self_pct, int *vcu_pct)
{
    if (self_pct) *self_pct = s_self_pct;
    if (vcu_pct) *vcu_pct = s_vcu_pct;
}
