#include "gateway_web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_handler.h"
#include "ota_handler.h"
#include "status_hub.h"
#include "ble_handler.h"
#include "vehicle_data.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "GW_WEB";
static httpd_handle_t s_server = NULL;

/* ────────────────────────────────────────────────────────────────────
 * Gömülü dashboard: koyu/neon tema, VCU dashboard'uyla aynı görsel dil
 * (Orbitron/Inter, cam efektli kartlar), farklı bilgi mimarisi — burası
 * bir sürüş göstergesi değil, bir yönetim konsolu.
 * ──────────────────────────────────────────────────────────────────── */
static const char *dashboard_html =
"<!DOCTYPE html><html lang='tr'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no'>"
"<meta name='theme-color' content='#050608'>"
"<title>Secure Gateway Panel</title>"
"<style>"
"@import url('https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700;900&family=Inter:wght@300;400;600;800&display=swap');"
"*{box-sizing:border-box;}"
"body{--c-main:#00d4ff;--c-glow:rgba(0,212,255,0.35);--c-bg:#050608;--c-ok:#00ff88;--c-bad:#ff3333;--c-warn:#ffaa00;"
"margin:0;padding:0;min-height:100vh;background:radial-gradient(circle at 50% 0%,#1a1e29 0%,#050608 60%);"
"color:#fff;font-family:'Inter',sans-serif;}"
".top-bar{display:flex;justify-content:space-between;align-items:center;padding:18px 28px;"
"background:rgba(255,255,255,0.03);backdrop-filter:blur(10px);border-bottom:1px solid rgba(255,255,255,0.06);"
"position:sticky;top:0;z-index:10;flex-wrap:wrap;gap:10px;}"
".brand{font-family:'Orbitron',sans-serif;font-weight:900;letter-spacing:2px;font-size:1.2rem;}"
".brand span{color:var(--c-main);}"
".ws-dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--c-bad);margin-right:6px;"
"box-shadow:0 0 8px var(--c-bad);transition:.3s;}"
".ws-dot.up{background:var(--c-ok);box-shadow:0 0 8px var(--c-ok);}"
".clock{font-family:'Orbitron',sans-serif;font-size:1.1rem;letter-spacing:1px;color:#8892b0;}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:18px;padding:22px;max-width:1200px;margin:0 auto;}"
".card{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.07);border-radius:16px;padding:20px;"
"backdrop-filter:blur(6px);}"
".card h2{margin:0 0 4px;font-size:1rem;letter-spacing:1px;text-transform:uppercase;color:#fff;}"
".card p.sub{margin:0 0 16px;color:#5a6580;font-size:.8rem;}"
".pill{display:inline-flex;align-items:center;gap:6px;padding:5px 12px;border-radius:20px;font-size:.78rem;"
"font-weight:700;background:rgba(255,255,255,0.05);margin:3px 6px 3px 0;}"
".pill.ok{color:var(--c-ok);}.pill.bad{color:var(--c-bad);}.pill.warn{color:var(--c-warn);}"
".row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid rgba(255,255,255,0.05);font-size:.85rem;}"
".row:last-child{border-bottom:none;}"
".row .k{color:#8892b0;}"
".row .v{font-family:'Orbitron',sans-serif;font-weight:700;}"
"input{width:100%;padding:10px 12px;margin-bottom:10px;border-radius:8px;border:1px solid rgba(255,255,255,0.12);"
"background:rgba(0,0,0,0.3);color:#fff;font-size:.9rem;}"
"input::placeholder{color:#4a5568;}"
"button{width:100%;padding:11px;border:none;border-radius:8px;background:var(--c-main);color:#001;"
"font-weight:800;letter-spacing:.5px;cursor:pointer;font-size:.85rem;transition:.2s;}"
"button:hover{filter:brightness(1.1);}button:disabled{opacity:.4;cursor:not-allowed;}"
".msg{margin-top:8px;font-size:.78rem;min-height:16px;}"
".msg.ok{color:var(--c-ok);}.msg.bad{color:var(--c-bad);}"
".bar-track{width:100%;height:8px;border-radius:4px;background:rgba(255,255,255,0.08);overflow:hidden;margin-top:8px;}"
".bar-fill{height:100%;background:var(--c-main);width:0%;transition:width .3s;box-shadow:0 0 8px var(--c-glow);}"
".log{max-height:170px;overflow-y:auto;font-size:.78rem;color:#c5cbe0;display:flex;flex-direction:column-reverse;gap:4px;}"
".log div{padding:4px 8px;background:rgba(255,255,255,0.03);border-radius:6px;}"
"</style></head><body>"
"<div class='top-bar'>"
"<div class='brand'><span>SECURE</span> GATEWAY</div>"
"<div><span id='wsdot' class='ws-dot'></span><span id='wslabel' style='font-size:.8rem;color:#8892b0;'>Bağlanıyor…</span></div>"
"<div class='clock' id='clock'>--:--:--</div>"
"</div>"
"<div class='grid'>"

"<div class='card'><h2>Bağlantı</h2><p class='sub'>WiFi ve BLE durumu</p>"
"<div class='row'><span class='k'>İnternet (STA)</span><span class='v' id='sta_pill'>—</span></div>"
"<div class='row'><span class='k'>STA IP</span><span class='v' id='sta_ip'>-</span></div>"
"<div class='row'><span class='k'>Yönetim ağı (AP)</span><span class='v' id='ap_ssid'>-</span></div>"
"<div class='row'><span class='k'>BLE (telefon/tablet)</span><span class='v' id='ble_pill'>—</span></div>"
"</div>"

"<div class='card'><h2>WiFi Ayarla</h2><p class='sub'>Gateway'in bağlanacağı ağı değiştirir</p>"
"<input id='w_ssid' placeholder='Ağ adı (SSID)'>"
"<input id='w_pass' type='password' placeholder='Şifre (en az 8 karakter)'>"
"<button onclick='sendWifi()'>Bağlan</button><div class='msg' id='w_msg'></div>"
"</div>"

"<div class='card'><h2>Gateway Güncelle</h2><p class='sub'>Kartın kendi yazılımı — HTTPS ile indirir</p>"
"<input id='o_url' placeholder='https://.../firmware.bin'>"
"<button onclick='sendOta(\"self\")'>Güncelle</button><div class='msg' id='o_msg'></div>"
"<div class='bar-track'><div class='bar-fill' id='o_bar'></div></div>"
"</div>"

"<div class='card'><h2>VCU Güncelle</h2><p class='sub'>Wi-Fi ile indirilir, CAN/UDS ile VCU'ya flashlanır (flash yavaştır, dakikalar sürebilir)</p>"
"<input id='v_url' placeholder='https://.../vcu_firmware.bin'>"
"<button onclick='sendOta(\"vcu\")'>Güncelle</button><div class='msg' id='v_msg'></div>"
"<div style='font-size:.72rem;color:#5a6580;margin-top:10px;'>İndirme</div>"
"<div class='bar-track'><div class='bar-fill' id='v_dl_bar'></div></div>"
"<div style='font-size:.72rem;color:#5a6580;margin-top:8px;'>VCU Flash (CAN/UDS)</div>"
"<div class='bar-track'><div class='bar-fill' id='v_flash_bar'></div></div>"
"</div>"

"<div class='card'><h2>VCU Özet</h2><p class='sub'>CAN üzerinden bildirilen son değerler</p>"
"<div class='row'><span class='k'>Batarya (SOC)</span><span class='v' id='vc_soc'>--%</span></div>"
"<div class='row'><span class='k'>Mod</span><span class='v' id='vc_mode'>-</span></div>"
"<div class='row'><span class='k'>Vites</span><span class='v' id='vc_gear'>-</span></div>"
"<div class='row'><span class='k'>Tork</span><span class='v' id='vc_torque'>0 Nm</span></div>"
"<div class='row'><span class='k'>DTC</span><span class='v' id='vc_dtc'>-</span></div>"
"<div class='row'><span class='k'>HVIL</span><span class='v' id='vc_hvil'>-</span></div>"
"<div class='row'><span class='k'>Firmware</span><span class='v' id='vc_fw'>-</span></div>"
"</div>"

"<div class='card'><h2>Durum Günlüğü</h2><p class='sub'>ESP32'den gelen son olaylar</p>"
"<div class='log' id='log'></div>"
"</div>"

"</div>"
"<script>"
"const MOD={1:'Beklemede',2:'Sürüşte',3:'Geri Vites',4:'Arıza',5:'Şarj Oluyor'};"
"setInterval(()=>{document.getElementById('clock').innerText=new Date().toLocaleTimeString('tr-TR');},1000);"
"let lastLog='';const logEl=document.getElementById('log');"
"function pushLog(t){if(t===lastLog)return;lastLog=t;const d=document.createElement('div');"
"d.textContent=new Date().toLocaleTimeString('tr-TR')+'  '+t;logEl.prepend(d);"
"while(logEl.children.length>10)logEl.removeChild(logEl.lastChild);}"
"function pill(el,ok,okText,badText){el.textContent=ok?okText:badText;el.className='v '+(ok?'pill ok':'pill bad');}"
"let ws;function connect(){"
"ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');"
"ws.onopen=()=>{document.getElementById('wsdot').classList.add('up');document.getElementById('wslabel').textContent='Canlı';};"
"ws.onclose=()=>{document.getElementById('wsdot').classList.remove('up');document.getElementById('wslabel').textContent='Bağlantı yok';setTimeout(connect,1500);};"
"ws.onmessage=(e)=>{const d=JSON.parse(e.data);"
"pill(document.getElementById('sta_pill'),d.sta_connected,'Bağlı','Bağlı değil');"
"document.getElementById('sta_ip').textContent=d.sta_ip;"
"document.getElementById('ap_ssid').textContent=d.ap_ssid;"
"pill(document.getElementById('ble_pill'),d.ble_connected,'Bağlı','Bağlı değil');"
"document.getElementById('o_bar').style.width=(d.self_ota_active?d.self_ota_pct:0)+'%';"
"document.getElementById('v_dl_bar').style.width=(d.vcu_ota_active?d.vcu_dl_pct:0)+'%';"
"document.getElementById('v_flash_bar').style.width=(d.vcu_ota_active?d.vcu_flash_pct:0)+'%';"
"const vc=d.vcu||{};"
"document.getElementById('vc_soc').textContent=(vc.soc??'--')+'%';"
"document.getElementById('vc_mode').textContent=MOD[vc.state]||'-';"
"document.getElementById('vc_gear').textContent=vc.gear||'-';"
"document.getElementById('vc_torque').textContent=(vc.torque??0)+' Nm';"
"document.getElementById('vc_dtc').textContent=vc.dtc||'-';"
"document.getElementById('vc_hvil').textContent=vc.hvil||'-';"
"document.getElementById('vc_fw').textContent=vc.fw_ver||'-';"
"if(d.status)pushLog(d.status);"
"};}connect();"
"async function postForm(url,data){"
"const body=Object.entries(data).map(([k,v])=>encodeURIComponent(k)+'='+encodeURIComponent(v)).join('&');"
"const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
"return r.json();}"
"async function sendWifi(){const msg=document.getElementById('w_msg');"
"try{const r=await postForm('/api/wifi',{ssid:document.getElementById('w_ssid').value,password:document.getElementById('w_pass').value});"
"msg.textContent=r.message;msg.className='msg '+(r.ok?'ok':'bad');}catch(e){msg.textContent='İstek başarısız';msg.className='msg bad';}}"
"async function sendOta(kind){const isVcu=kind==='vcu';const urlInput=document.getElementById(isVcu?'v_url':'o_url');"
"const msg=document.getElementById(isVcu?'v_msg':'o_msg');"
"try{const r=await postForm(isVcu?'/api/ota/vcu':'/api/ota/self',{url:urlInput.value});"
"msg.textContent=r.message;msg.className='msg '+(r.ok?'ok':'bad');}catch(e){msg.textContent='İstek başarısız';msg.className='msg bad';}}"
"</script></body></html>";

/* ── Yardımcılar ─────────────────────────────────────────────────── */

static void url_decode(char *s)
{
    char *dst = s;
    while (*s) {
        if (*s == '+') {
            *dst++ = ' ';
            s++;
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *dst++ = *s++;
        }
    }
    *dst = '\0';
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    int total = req->content_len;
    if (total <= 0 || total >= (int)buf_size) return ESP_FAIL;

    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) return ESP_FAIL;
        received += r;
    }
    buf[received] = '\0';
    return ESP_OK;
}

static esp_err_t send_json_result(httpd_req_t *req, bool ok, const char *message)
{
    httpd_resp_set_type(req, "application/json");
    char out[192];
    snprintf(out, sizeof(out), "{\"ok\":%s,\"message\":\"%s\"}", ok ? "true" : "false", message);
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

/* ── HTTP handler'lar ────────────────────────────────────────────── */

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, dashboard_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_wifi_handler(httpd_req_t *req)
{
    char body[300];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_json_result(req, false, "İstek gövdesi okunamadı");
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK) ssid[0] = '\0';
    if (httpd_query_key_value(body, "password", pass, sizeof(pass)) != ESP_OK) pass[0] = '\0';
    url_decode(ssid);
    url_decode(pass);

    if (strlen(ssid) == 0) {
        return send_json_result(req, false, "SSID gerekli");
    }
    if (strlen(pass) < 8) {
        return send_json_result(req, false, "Şifre en az 8 karakter olmalı");
    }

    wifi_connect(ssid, pass);
    return send_json_result(req, true, "Baglaniliyor...");
}

static esp_err_t api_ota_self_handler(httpd_req_t *req)
{
    char body[700];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_json_result(req, false, "İstek gövdesi okunamadı");
    }
    char url[512] = {0};
    if (httpd_query_key_value(body, "url", url, sizeof(url)) != ESP_OK) url[0] = '\0';
    url_decode(url);

    if (strlen(url) < 10) {
        return send_json_result(req, false, "Gecerli bir URL girin");
    }
    esp_err_t ret = ota_start(url);
    return send_json_result(req, ret == ESP_OK,
                             ret == ESP_OK ? "Gateway guncellemesi baslatildi"
                                           : "Baslatilamadi (zaten calisiyor olabilir)");
}

static esp_err_t api_ota_vcu_handler(httpd_req_t *req)
{
    char body[700];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_json_result(req, false, "İstek gövdesi okunamadı");
    }
    char url[512] = {0};
    if (httpd_query_key_value(body, "url", url, sizeof(url)) != ESP_OK) url[0] = '\0';
    url_decode(url);

    if (strlen(url) < 10) {
        return send_json_result(req, false, "Gecerli bir URL girin");
    }
    esp_err_t ret = vcu_fw_flash(url);
    return send_json_result(req, ret == ESP_OK,
                             ret == ESP_OK ? "VCU guncellemesi baslatildi"
                                           : "Baslatilamadi (zaten calisiyor olabilir)");
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Yeni WS baglantisi: fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    /* Sadece server->client yayın kullanıyoruz; gelen frame'i yut. */
    uint8_t buf[16];
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.payload = buf;
    httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf) - 1);
    return ESP_OK;
}

/* ── WebSocket yayın görevi (1Hz) ────────────────────────────────── */

struct ws_send_arg {
    int fd;
    char *json;
};

static void send_ws_data_sync(void *arg)
{
    struct ws_send_arg *a = arg;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.payload = (uint8_t *)a->json;
    ws_pkt.len     = strlen(a->json);
    ws_pkt.type    = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(s_server, a->fd, &ws_pkt);
    free(a->json);
    free(a);
}

static void ws_broadcast_task(void *pv)
{
    char json[900];

    while (1) {
        if (!s_server) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        char sta_ip[16];
        char ap_ssid[24];
        char status[128];
        char vjson[300];
        int  self_pct = 0, vcu_dl_pct = 0, vcu_flash_pct = 0;

        wifi_get_sta_ip(sta_ip, sizeof(sta_ip));
        wifi_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
        status_hub_get_last(status, sizeof(status));
        status_hub_get_progress(&self_pct, &vcu_dl_pct, &vcu_flash_pct);
        vehicle_data_to_json(vjson, sizeof(vjson));

        snprintf(json, sizeof(json),
            "{"
            "\"sta_connected\":%s,\"sta_ip\":\"%s\","
            "\"ap_ssid\":\"%s\",\"ble_connected\":%s,"
            "\"self_ota_active\":%s,\"self_ota_pct\":%d,"
            "\"vcu_ota_active\":%s,\"vcu_dl_pct\":%d,\"vcu_flash_pct\":%d,"
            "\"status\":\"%s\","
            "\"vcu\":%s"
            "}",
            wifi_is_connected() ? "true" : "false", sta_ip,
            ap_ssid, ble_is_connected() ? "true" : "false",
            ota_is_running() ? "true" : "false", self_pct,
            vcu_fw_is_running() ? "true" : "false", vcu_dl_pct, vcu_flash_pct,
            status,
            vjson
        );

        size_t clients = 8;
        int client_fds[8];
        if (httpd_get_client_list(s_server, &clients, client_fds) == ESP_OK) {
            for (int i = 0; i < clients; i++) {
                struct ws_send_arg *a = malloc(sizeof(struct ws_send_arg));
                if (!a) continue;
                a->fd = client_fds[i];
                a->json = strdup(json);
                if (!a->json) {
                    free(a);
                    continue;
                }
                if (httpd_queue_work(s_server, send_ws_data_sync, a) != ESP_OK) {
                    free(a->json);
                    free(a);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ── Başlatma ────────────────────────────────────────────────────── */

void gateway_web_server_start_task(void *pv)
{
    ESP_LOGI(TAG, "Gateway web panel Core 0 task basliyor...");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority   = 3;
    config.stack_size      = 8192;
    config.core_id         = 0;
    config.max_uri_handlers = 10;

    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
        httpd_register_uri_handler(s_server, &root);

        httpd_uri_t ws = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
        httpd_register_uri_handler(s_server, &ws);

        httpd_uri_t api_wifi = { .uri = "/api/wifi", .method = HTTP_POST, .handler = api_wifi_handler };
        httpd_register_uri_handler(s_server, &api_wifi);

        httpd_uri_t api_ota_self = { .uri = "/api/ota/self", .method = HTTP_POST, .handler = api_ota_self_handler };
        httpd_register_uri_handler(s_server, &api_ota_self);

        httpd_uri_t api_ota_vcu = { .uri = "/api/ota/vcu", .method = HTTP_POST, .handler = api_ota_vcu_handler };
        httpd_register_uri_handler(s_server, &api_ota_vcu);

        ESP_LOGI(TAG, "Yonetim paneli yayinda (Core 0, WebSocket)");

        xTaskCreatePinnedToCore(ws_broadcast_task, "gw_ws_bcast", 4096, NULL, 3, NULL, 0);
    } else {
        ESP_LOGE(TAG, "HTTP server baslatilamadi!");
    }

    vTaskDelete(NULL);
}
