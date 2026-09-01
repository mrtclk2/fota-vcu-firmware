#include "gateway_web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_handler.h"
#include "ota_handler.h"
#include "vcu_releases.h"
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
 * (Orbitron/Inter, cam efektli kartlar, radyal göstergeler) — burası bir
 * sürüş göstergesi değil, bir yönetim konsolu. Gateway'in kendi firmware'i
 * güncellenmez (bench-test senaryosunda buna gerek yok); burada tek OTA
 * hedefi VCU'dur, kaynağı da GitHub release listesidir (elle link girilmez).
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
".note{font-size:.72rem;color:#5a6580;margin-top:10px;line-height:1.5;}"
"input{width:100%;padding:10px 12px;margin-bottom:10px;border-radius:8px;border:1px solid rgba(255,255,255,0.12);"
"background:rgba(0,0,0,0.3);color:#fff;font-size:.9rem;}"
"input::placeholder{color:#4a5568;}"
"button{width:100%;padding:11px;border:none;border-radius:8px;background:var(--c-main);color:#001;"
"font-weight:800;letter-spacing:.5px;cursor:pointer;font-size:.85rem;transition:.2s;}"
"button:hover{filter:brightness(1.1);}button:disabled{opacity:.4;cursor:not-allowed;}"
".btn-secondary{background:transparent;border:1px solid rgba(255,255,255,0.15);color:#c5cbe0;}"
".btn-row{display:flex;gap:10px;}.btn-row button{width:auto;flex:1;}"
".msg{margin-top:8px;font-size:.78rem;min-height:16px;}"
".msg.ok{color:var(--c-ok);}.msg.bad{color:var(--c-bad);}"
".bar-track{width:100%;height:8px;border-radius:4px;background:rgba(255,255,255,0.08);overflow:hidden;margin-top:8px;}"
".bar-fill{height:100%;background:var(--c-main);width:0%;transition:width .3s;box-shadow:0 0 8px var(--c-glow);}"
".log{max-height:170px;overflow-y:auto;font-size:.78rem;color:#c5cbe0;display:flex;flex-direction:column-reverse;gap:4px;}"
".log div{padding:4px 8px;background:rgba(255,255,255,0.03);border-radius:6px;}"
".modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.65);z-index:50;align-items:center;justify-content:center;padding:16px;}"
".modal-overlay.show{display:flex;}"
".modal-box{background:#0c0e14;border:1px solid rgba(255,255,255,.1);border-radius:16px;padding:20px;width:100%;max-width:420px;max-height:80vh;overflow-y:auto;}"
".modal-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;}"
".modal-head h3{margin:0;font-size:1rem;letter-spacing:1px;}"
".btn-x{width:auto;padding:2px 10px;background:transparent;border:1px solid rgba(255,255,255,.15);color:#8892b0;font-size:1rem;border-radius:6px;}"
".net-row{display:flex;justify-content:space-between;align-items:center;padding:11px 6px;border-bottom:1px solid rgba(255,255,255,.05);cursor:pointer;font-size:.85rem;}"
".net-row:hover{background:rgba(255,255,255,.04);}"
".net-meta{color:#5a6580;font-size:.75rem;}"
".rel-row{display:flex;justify-content:space-between;align-items:center;padding:10px 6px;border-bottom:1px solid rgba(255,255,255,.05);gap:10px;}"
".rel-tag{font-family:'Orbitron',sans-serif;font-weight:700;font-size:.85rem;color:var(--c-main);}"
".rel-name{font-size:.72rem;color:#8892b0;margin-top:2px;}"
".btn-flash{width:auto;padding:8px 16px;flex-shrink:0;}"
".mgauge-row{display:flex;justify-content:center;gap:24px;margin-bottom:16px;}"
".mgauge-item{display:flex;flex-direction:column;align-items:center;gap:6px;}"
".mgauge-wrap{position:relative;width:100px;height:100px;}"
".mgauge-svg{width:100px;height:100px;transform:rotate(-90deg);}"
".mgauge-bg{fill:none;stroke:rgba(255,255,255,.08);stroke-width:9;}"
".mgauge-fill{fill:none;stroke:var(--c-main);stroke-width:9;stroke-linecap:round;stroke-dasharray:0 301.6;transition:stroke-dasharray .4s;}"
".mgauge-center{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;}"
".mgauge-val{font-family:'Orbitron',sans-serif;font-weight:800;font-size:1.2rem;}"
".mgauge-unit{font-size:.6rem;color:#5a6580;}"
".mgauge-label{font-size:.68rem;color:#8892b0;letter-spacing:1px;text-transform:uppercase;}"
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
"<div class='note'>Yönetim ağı (AP) internet bağlantısından (STA) bağımsız çalışır: gateway başka bir WiFi'ye bağlansa bile tablet bu ağa bağlı kaldığı sürece paneli kullanmaya devam eder.</div>"
"</div>"

"<div class='card'><h2>WiFi Ayarla</h2><p class='sub'>Gateway'in internet için bağlanacağı ağı değiştirir</p>"
"<button onclick='openWifiModal()'>Ağ Tara ve Bağlan</button>"
"<div class='msg' id='w_msg'></div>"
"</div>"

"<div class='card'><h2>VCU Yazılım Güncelleme</h2><p class='sub'>GitHub release listesinden seç, CAN/UDS ile VCU'ya flashlanır (flash yavaştır, dakikalar sürebilir)</p>"
"<button class='btn-secondary' onclick='loadReleases()'>Yazılımları Yenile</button>"
"<div id='relList' style='margin-top:6px;'></div>"
"<div style='font-size:.72rem;color:#5a6580;margin-top:14px;'>İndirme</div>"
"<div class='bar-track'><div class='bar-fill' id='v_dl_bar'></div></div>"
"<div style='font-size:.72rem;color:#5a6580;margin-top:8px;'>VCU Flash (CAN/UDS)</div>"
"<div class='bar-track'><div class='bar-fill' id='v_flash_bar'></div></div>"
"<div class='msg' id='v_msg'></div>"
"</div>"

"<div class='card'><h2>VCU Özet</h2><p class='sub'>CAN üzerinden bildirilen son değerler</p>"
"<div class='mgauge-row'>"
"<div class='mgauge-item'><div class='mgauge-wrap'><svg class='mgauge-svg' viewBox='0 0 120 120'>"
"<circle class='mgauge-bg' cx='60' cy='60' r='48'></circle>"
"<circle id='g_soc' class='mgauge-fill' cx='60' cy='60' r='48'></circle></svg>"
"<div class='mgauge-center'><div class='mgauge-val' id='vc_soc'>--</div><div class='mgauge-unit'>%</div></div></div>"
"<div class='mgauge-label'>Batarya</div></div>"
"<div class='mgauge-item'><div class='mgauge-wrap'><svg class='mgauge-svg' viewBox='0 0 120 120'>"
"<circle class='mgauge-bg' cx='60' cy='60' r='48'></circle>"
"<circle id='g_thr' class='mgauge-fill' cx='60' cy='60' r='48' style='stroke:#ffaa00'></circle></svg>"
"<div class='mgauge-center'><div class='mgauge-val' id='vc_thr'>--</div><div class='mgauge-unit'>%</div></div></div>"
"<div class='mgauge-label'>Gaz Pedalı</div></div>"
"</div>"
"<div class='row'><span class='k'>Tork</span><span class='v' id='vc_torque'>0 Nm</span></div>"
"<div class='bar-track'><div class='bar-fill' id='vc_torque_bar' style='background:#ffaa00'></div></div>"
"<div class='row' style='margin-top:6px;'><span class='k'>Mod</span><span class='v' id='vc_mode'>-</span></div>"
"<div class='row'><span class='k'>Vites</span><span class='v' id='vc_gear'>-</span></div>"
"<div class='row'><span class='k'>Fren</span><span class='v' id='vc_brake'>-</span></div>"
"<div class='row'><span class='k'>DTC</span><span class='v' id='vc_dtc'>-</span></div>"
"<div class='row'><span class='k'>HVIL</span><span class='v' id='vc_hvil'>-</span></div>"
"<div class='row'><span class='k'>Firmware</span><span class='v' id='vc_fw'>-</span></div>"
"</div>"

"<div class='card'><h2>Durum Günlüğü</h2><p class='sub'>ESP32'den gelen son olaylar</p>"
"<div class='log' id='log'></div>"
"</div>"

"</div>"

"<div id='wifiModal' class='modal-overlay'>"
"<div class='modal-box'>"
"<div class='modal-head'><h3>WiFi Ağları</h3><button class='btn-x' onclick='closeWifiModal()'>×</button></div>"
"<div id='wifiListView'>"
"<div id='wifiScanMsg' style='color:#8892b0;font-size:.85rem;padding:10px 0;'>Taranıyor…</div>"
"<div id='wifiList'></div>"
"</div>"
"<div id='wifiPassView' style='display:none;'>"
"<p style='color:#8892b0;font-size:.85rem;margin:0 0 10px;'>Seçilen ağ: <b id='wifiSelSsid' style='color:#fff;'></b></p>"
"<input id='wifiPassInput' type='password' placeholder='Şifre (en az 8 karakter)'>"
"<div class='btn-row'><button class='btn-secondary' onclick='backToListView()'>Geri</button><button onclick='confirmWifiConnect()'>Bağlan</button></div>"
"<div class='msg' id='wifiConnMsg'></div>"
"</div>"
"</div></div>"

"<script>"
"const MOD={1:'Beklemede',2:'Sürüşte',3:'Geri Vites',4:'Arıza',5:'Şarj Oluyor'};"
"const CIRC=301.6;"
"function nz(v,fb){return (v===undefined||v===null)?fb:v;}"
"function setGauge(id,pct){document.getElementById(id).style.strokeDasharray=(Math.max(0,Math.min(100,pct))/100*CIRC)+' '+CIRC;}"
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
"document.getElementById('v_dl_bar').style.width=(d.vcu_ota_active?d.vcu_dl_pct:0)+'%';"
"document.getElementById('v_flash_bar').style.width=(d.vcu_ota_active?d.vcu_flash_pct:0)+'%';"
"const vc=d.vcu||{};"
"setGauge('g_soc',vc.soc||0);document.getElementById('vc_soc').textContent=nz(vc.soc,'--');"
"setGauge('g_thr',vc.throttle||0);document.getElementById('vc_thr').textContent=nz(vc.throttle,'--');"
"document.getElementById('vc_torque').textContent=nz(vc.torque,0)+' Nm';"
"document.getElementById('vc_torque_bar').style.width=Math.min(100,(nz(vc.torque,0)/500)*100)+'%';"
"document.getElementById('vc_mode').textContent=MOD[vc.state]||'-';"
"document.getElementById('vc_gear').textContent=vc.gear||'-';"
"document.getElementById('vc_brake').textContent=vc.brake?'Basılı':'Serbest';"
"document.getElementById('vc_dtc').textContent=vc.dtc||'-';"
"document.getElementById('vc_hvil').textContent=vc.hvil||'-';"
"document.getElementById('vc_fw').textContent=vc.fw_ver||'-';"
"if(d.status)pushLog(d.status);"
"};}connect();"
"async function postForm(url,data){"
"const body=Object.entries(data).map(([k,v])=>encodeURIComponent(k)+'='+encodeURIComponent(v)).join('&');"
"const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
"return r.json();}"

"let networks=[];let selectedNet=null;"
"function openWifiModal(){document.getElementById('wifiModal').classList.add('show');backToListView();scanNetworks();}"
"function closeWifiModal(){document.getElementById('wifiModal').classList.remove('show');}"
"function backToListView(){document.getElementById('wifiListView').style.display='block';document.getElementById('wifiPassView').style.display='none';}"
"function sigLabel(rssi){if(rssi>=-55)return'Güçlü';if(rssi>=-70)return'Orta';return'Zayıf';}"
"async function scanNetworks(){const msg=document.getElementById('wifiScanMsg');const listEl=document.getElementById('wifiList');"
"msg.style.display='block';msg.textContent='Taranıyor…';listEl.innerHTML='';"
"try{const r=await fetch('/api/wifi/scan');const d=await r.json();networks=d.networks||[];"
"if(networks.length===0){msg.textContent='Ağ bulunamadı';return;}"
"msg.style.display='none';"
"listEl.innerHTML=networks.map((n,i)=>`<div class=\"net-row\" onclick=\"selectNetwork(${i})\"><span>${n.ssid}</span>"
"<span class=\"net-meta\">${sigLabel(n.rssi)}${n.secure?' • Şifreli':' • Açık'}</span></div>`).join('');"
"}catch(e){msg.textContent='Tarama başarısız';}}"
"function selectNetwork(i){selectedNet=networks[i];document.getElementById('wifiSelSsid').textContent=selectedNet.ssid;"
"document.getElementById('wifiListView').style.display='none';document.getElementById('wifiPassView').style.display='block';"
"document.getElementById('wifiPassInput').value='';document.getElementById('wifiConnMsg').textContent='';}"
"async function confirmWifiConnect(){if(!selectedNet)return;const pass=document.getElementById('wifiPassInput').value;const msg=document.getElementById('wifiConnMsg');"
"try{const r=await postForm('/api/wifi',{ssid:selectedNet.ssid,password:pass});msg.textContent=r.message;msg.className='msg '+(r.ok?'ok':'bad');"
"if(r.ok)setTimeout(closeWifiModal,1500);}catch(e){msg.textContent='İstek başarısız';msg.className='msg bad';}}"

"let releases=[];"
"async function loadReleases(){const listEl=document.getElementById('relList');"
"listEl.innerHTML='<div style=\"color:#8892b0;font-size:.82rem;\">Yükleniyor…</div>';"
"try{const r=await fetch('/api/vcu/releases');const d=await r.json();releases=d.releases||[];"
"if(releases.length===0){listEl.innerHTML='<div style=\"color:#8892b0;font-size:.82rem;\">Hic .bin bulunamadi. GitHub uzerinde bir Release olusturup .bin dosyasi ekleyin.</div>';return;}"
"listEl.innerHTML=releases.map((r,i)=>`<div class=\"rel-row\"><div><div class=\"rel-tag\">${r.tag}</div>"
"<div class=\"rel-name\">${r.name} (${Math.round(r.size/1024)} KB)</div></div>"
"<button class=\"btn-flash\" onclick=\"flashRelease(${i})\">Flashla</button></div>`).join('');"
"}catch(e){listEl.innerHTML='<div class=\"msg bad\">Liste alinamadi</div>';}}"
"function flashRelease(i){const r=releases[i];if(!r)return;"
"if(!confirm('Secilen surum VCU icin flashlanacak: '+r.tag+' - '+r.name+'. Emin misiniz?'))return;"
"const msg=document.getElementById('v_msg');"
"postForm('/api/ota/vcu',{url:r.url}).then(res=>{msg.textContent=res.message;msg.className='msg '+(res.ok?'ok':'bad');})"
".catch(()=>{msg.textContent='Istek basarisiz';msg.className='msg bad';});}"
"loadReleases();"
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

static esp_err_t api_wifi_scan_handler(httpd_req_t *req)
{
    char *nets = malloc(2048);
    if (!nets) return httpd_resp_send_500(req);

    if (wifi_scan_json(nets, 2048) != ESP_OK) {
        strcpy(nets, "[]");
    }

    char *out = malloc(2048 + 32);
    if (!out) {
        free(nets);
        return httpd_resp_send_500(req);
    }
    snprintf(out, 2048 + 32, "{\"networks\":%s}", nets);
    free(nets);

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    free(out);
    return ret;
}

static esp_err_t api_vcu_releases_handler(httpd_req_t *req)
{
    char *rel = malloc(4096);
    if (!rel) return httpd_resp_send_500(req);

    if (vcu_releases_fetch_json(rel, 4096) != ESP_OK) {
        strcpy(rel, "[]");
    }

    char *out = malloc(4096 + 32);
    if (!out) {
        free(rel);
        return httpd_resp_send_500(req);
    }
    snprintf(out, 4096 + 32, "{\"releases\":%s}", rel);
    free(rel);

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    free(out);
    return ret;
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
            "\"vcu_ota_active\":%s,\"vcu_dl_pct\":%d,\"vcu_flash_pct\":%d,"
            "\"status\":\"%s\","
            "\"vcu\":%s"
            "}",
            wifi_is_connected() ? "true" : "false", sta_ip,
            ap_ssid, ble_is_connected() ? "true" : "false",
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

        httpd_uri_t api_wifi_scan = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = api_wifi_scan_handler };
        httpd_register_uri_handler(s_server, &api_wifi_scan);

        httpd_uri_t api_vcu_releases = { .uri = "/api/vcu/releases", .method = HTTP_GET, .handler = api_vcu_releases_handler };
        httpd_register_uri_handler(s_server, &api_vcu_releases);

        httpd_uri_t api_ota_vcu = { .uri = "/api/ota/vcu", .method = HTTP_POST, .handler = api_ota_vcu_handler };
        httpd_register_uri_handler(s_server, &api_ota_vcu);

        ESP_LOGI(TAG, "Yonetim paneli yayinda (Core 0, WebSocket)");

        xTaskCreatePinnedToCore(ws_broadcast_task, "gw_ws_bcast", 4096, NULL, 3, NULL, 0);
    } else {
        ESP_LOGE(TAG, "HTTP server baslatilamadi!");
    }

    vTaskDelete(NULL);
}
