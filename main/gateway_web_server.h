#pragma once

/**
 * @file gateway_web_server.h
 * @brief Secure Gateway Yönetim Paneli — HTTP + WebSocket dashboard.
 *
 * VCU'daki `vcu_web_server.c` ile aynı desende: ESP32 kendi üzerinde bir
 * web sunucusu barındırır. Farkı, VCU'nun aksine gateway WIFI_MODE_APSTA
 * çalışır (bkz. wifi_handler.c) — hem araç/ev WiFi'sine bağlı kalır
 * (internet + HTTPS OTA indirme için) hem de kendi yönetim ağını
 * (SECUREGW_XXXXXX) yayınlar, böylece bir teknisyen tableti araç WiFi
 * şifresini bilmeden doğrudan gateway'e bağlanıp paneli açabilir.
 *
 * Kullanım (app_main içinde, wifi_init() sonrasında):
 *   xTaskCreatePinnedToCore(gateway_web_server_start_task, "gw_web",
 *                           8192, NULL, 3, NULL, 0);
 */
void gateway_web_server_start_task(void *pv);
