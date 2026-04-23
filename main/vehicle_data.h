#pragma once
 
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
 
/* ── Araç Modu (State) Enum ── */
typedef enum {
    VEHICLE_STATE_STANDBY = 1,
    VEHICLE_STATE_DRIVE   = 2,
    VEHICLE_STATE_REVERSE = 3,
    VEHICLE_STATE_FAULT   = 4,
    VEHICLE_STATE_CHARGE  = 5
} vehicle_state_t;
 
/* ── Vites Durumu Enum ── */
typedef enum {
    GEAR_NEUTRAL  = 0,
    GEAR_DRIVE    = 1,
    GEAR_REVERSE  = 2
} gear_t;
 
/* ── Araç Veri Yapısı ── */
typedef struct {
    uint8_t         soc;            /* Batarya: %0-100          */
    vehicle_state_t state;          /* Araç modu: 1-5           */
    gear_t          gear;           /* Vites: N/D/R             */
    uint16_t        torque;         /* Hedef tork: 0-500 Nm     */
    uint8_t         throttle;       /* Gaz pedalı: %0-100       */
    bool            brake;          /* Fren durumu: true/false  */
    char            dtc[16];        /* Aktif DTC: "P0A0D" veya "NONE" */
    bool            hvil_alarm;     /* Güvenlik hattı: false=OK, true=ALARM */
    char            fw_version[16]; /* Yazılım sürümü: "v1.0.2" */
} vehicle_data_t;
 
/**
 * @brief Araç veri modülünü başlatır, BLE notify task'ını başlatır
 */
esp_err_t vehicle_data_init(void);
 
/**
 * @brief Araç verisini günceller.
 *        Kritik alan değişmişse BLE notify hemen tetiklenir.
 * @param new_data Yeni araç verisi
 */
void vehicle_data_update(const vehicle_data_t *new_data);
 
/**
 * @brief Mevcut araç verisini döner
 */
vehicle_data_t vehicle_data_get(void);
 
/**
 * @brief BLE notify için JSON string üretir
 * @param buf   Çıktı buffer
 * @param size  Buffer boyutu
 */
void vehicle_data_to_json(char *buf, size_t size);
 