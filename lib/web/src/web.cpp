#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "app_config.h"
#include "shared.h"

// ====== Wi-Fi AP creds (same as your UNO build) ======
static const char* AP_SSID = "VesaliusSimUse";
static const char* AP_PASS = "Vesal1us";

// ====== Async web server and SSE source ======
static AsyncWebServer server(80);
static AsyncEventSource sse("/stream");

// ====== Minimal dark UI (you’ll recognize the vibe; charts later) ======
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>VSU ESP32</title>
<style>
  :root{--bg:#111418;--panel:#171b20;--ink:#e6edf3;--muted:#9aa7b2;--grid:#26303a;--acc:#0ea5e9;--ok:#22c55e;--warn:#ef4444}
  *{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,Segoe UI,Roboto,Arial}
  .wrap{max-width:980px;margin:16px auto;padding:0 12px}
  .card{background:var(--panel);border:1px solid var(--grid);border-radius:12px;padding:12px}
  h1{font-size:18px;margin:0 0 8px 0}
  .row{display:flex;gap:10px;flex-wrap:wrap}
  .box{flex:1 1 250px;background:#0f1317;border:1px solid var(--grid);border-radius:10px;padding:10px}
  label{display:block;font-size:12px;color:var(--muted);margin-bottom:6px}
  input[type="range"]{width:100%}
  select,button{background:#0f1317;color:var(--ink);border:1px solid var(--grid);border-radius:8px;padding:6px 8px}
  .pill{display:inline-block;font-size:12px;padding:3px 8px;border:1px solid var(--grid);border-radius:999px;color:var(--muted)}
  .kv{display:grid;grid-template-columns:auto 1fr;gap:6px;align-items:center}
  .kv b{color:var(--muted);font-weight:600}
</style>
</head><body><div class="wrap">
  <div class="card">
    <h1>VSU ESP32 — Live Control</h1>
    <div class="row" style="margin-bottom:12px">
      <span class="pill">SSE: <b id="sse">INIT</b></span>
      <span class="pill">Hz: <b id="hz">—</b></span>
      <span class="pill">Loop ms: <b id="loop">—</b></span>
      <span class="pill">Mode: <b id="mode">—</b></span>
      <span class="pill">Paused: <b id="paused">—</b></span>
    </div>
    <div class="row">
      <div class="box">
        <label>Pause / Resume</label>
        <button id="btnPause">Toggle Pause</button>
      </div>
      <div class="box">
        <label>Mode</label>
        <select id="selMode">
          <option value="0">Forward</option>
          <option value="1">Reverse</option>
          <option value="2">Beat</option>
        </select>
      </div>
      <div class="box">
        <label>Power (%)</label>
        <input id="rngPower" type="range" min="10" max="100" step="1">
        <div><span id="powerLabel">—</span></div>
      </div>
      <div class="box">
        <label>BPM</label>
        <input id="rngBpm" type="range" min="0.5" max="60" step="0.5">
        <div><span id="bpmLabel">—</span></div>
      </div>
    </div>
  </div>

  <div class="card" style="margin-top:12px">
    <h1>Telemetry</h1>
    <div class="kv">
      <b>Ventricle (mmHg):</b> <span id="vent">—</span>
      <b>Atrium (mmHg):</b>    <span id="atr">—</span>
      <b>Flow (mL/min):</b>    <span id="flow">—</span>
      <b>PWM:</b>              <span id="pwm">—</span>
      <b>Valve:</b>            <span id="valve">—</span>
    </div>
  </div>
</div>
<script>
const $=id=>document.getElementById(id);
let lastT=performance.now(), emaHz=0;
const es = new EventSource('/stream');
es.onopen = ()=>{ $('sse').textContent='OPEN'; };
es.onerror= ()=>{ $('sse').textContent='ERROR'; };
es.onmessage = ev=>{
  const now=performance.now(), dt=now-lastT; lastT=now;
  const hz = 1000/Math.max(1,dt); emaHz = emaHz? (emaHz*0.9+hz*0.1) : hz;
  $('hz').textContent = emaHz.toFixed(1);

  try{
    const d = JSON.parse(ev.data);
    $('vent').textContent  = (d.vent_mmHg??0).toFixed(1);
    $('atr').textContent   = (d.atr_mmHg??0).toFixed(1);
    $('flow').textContent  = (d.flow_ml_min??0).toFixed(1);
    $('pwm').textContent   = d.pwm??0;
    $('valve').textContent = d.valve? 'FWD':'REV';
    $('mode').textContent  = (['FWD','REV','BEAT'][d.mode||0]);
    $('paused').textContent= d.paused? 'YES':'NO';
    $('loop').textContent  = (d.loopMs??0).toFixed(2)+' ms';

    // reflect current settings in controls
    $('selMode').value = String(d.mode||0);
    $('rngPower').value = String(d.powerPct||30);
    $('powerLabel').textContent = (d.powerPct||30)+'%';
    $('rngBpm').value = String(d.bpm||5);
    $('bpmLabel').textContent = (d.bpm||5).toFixed(1)+' BPM';
  }catch(e){}
};

// control endpoints
$('btnPause').onclick=()=>fetch('/api/toggle').then(()=>{});
$('selMode').onchange=()=>fetch('/api/mode?m='+encodeURIComponent($('selMode').value)).then(()=>{});
$('rngPower').oninput =()=>{ $('powerLabel').textContent=$('rngPower').value+'%'; };
$('rngPower').onchange=()=>fetch('/api/power?pct='+encodeURIComponent($('rngPower').value)).then(()=>{});
$('rngBpm').oninput   =()=>{ $('bpmLabel').textContent = Number($('rngBpm').value).toFixed(1)+' BPM'; };
$('rngBpm').onchange  =()=>fetch('/api/bpm?b='+encodeURIComponent($('rngBpm').value)).then(()=>{});
</script>
</body></html>)HTML";

// ====== Tiny helpers ======
static inline void postOrInline(const Cmd& c){
  if (!shared_cmd_post(c)) {
    // Fallback: apply immediately (rare)
    switch(c.type){
      case CMD_TOGGLE_PAUSE: {
        int p=G.paused.load(std::memory_order_relaxed);
        G.paused.store(p?0:1,std::memory_order_relaxed);
      } break;
      case CMD_SET_POWER_PCT: {
        int pct=c.iarg; if(pct<10)pct=10; if(pct>100)pct=100;
        G.powerPct.store(pct,std::memory_order_relaxed);
      } break;
      case CMD_SET_MODE: {
        int m=c.iarg; if(m<0||m>2)m=0;
        G.mode.store(m,std::memory_order_relaxed);
      } break;
      case CMD_SET_BPM: {
        float b=c.farg; if(b<0.5f)b=0.5f; if(b>60.0f)b=60.0f;
        G.bpm.store(b,std::memory_order_relaxed);
      } break;
    }
  }
}

// ====== 30 Hz SSE task pinned to Core 0 ======
static void sse_task(void*) {
  const TickType_t period = pdMS_TO_TICKS(33); // ~30 Hz
  TickType_t wake = xTaskGetTickCount();
  static char buf[256];

  for(;;){
    // Snapshot atomics
    int    paused = G.paused.load(std::memory_order_relaxed);
    int    mode   = G.mode.load(std::memory_order_relaxed);
    int    power  = G.powerPct.load(std::memory_order_relaxed);
    unsigned pwm  = G.pwm.load(std::memory_order_relaxed);
    unsigned valv = G.valve.load(std::memory_order_relaxed);
    float  vent   = G.vent_mmHg.load(std::memory_order_relaxed);
    float  atr    = G.atr_mmHg.load(std::memory_order_relaxed);
    float  flow   = G.flow_ml_min.load(std::memory_order_relaxed);
    float  bpm    = G.bpm.load(std::memory_order_relaxed);
    float  loop   = G.loopMs.load(std::memory_order_relaxed);

    int n = snprintf(buf, sizeof(buf),
      "{\"paused\":%d,\"mode\":%d,\"powerPct\":%d,"
      "\"pwm\":%u,\"valve\":%u,"
      "\"vent_mmHg\":%.1f,\"atr_mmHg\":%.1f,\"flow_ml_min\":%.1f,"
      "\"bpm\":%.1f,\"loopMs\":%.2f}",
      paused, mode, power, pwm, valv, vent, atr, flow, bpm, loop);

    if (n>0) sse.send(buf, "message", millis());
    vTaskDelayUntil(&wake, period);
  }
}

void web_start() {
  // ---- Wi-Fi AP ----
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WEB] AP %s (%s)\n", ok?"started":"FAILED", ip.toString().c_str());

  // ---- Simple routes ----
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });

  // Toggle pause
  server.on("/api/toggle", HTTP_GET, [](AsyncWebServerRequest* req){
    Cmd c{ CMD_TOGGLE_PAUSE, 0, 0.0f }; postOrInline(c);
    req->send(204);
  });

  // Set mode (?m=0/1/2)
  server.on("/api/mode", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("m")) {
      int m = req->getParam("m")->value().toInt();
      Cmd c{ CMD_SET_MODE, m, 0.0f }; postOrInline(c);
    }
    req->send(204);
  });

  // Set power (?pct=10..100)
  server.on("/api/power", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("pct")) {
      int pct = req->getParam("pct")->value().toInt();
      if (pct<10) pct=10; if(pct>100)pct=100;
      Cmd c{ CMD_SET_POWER_PCT, pct, 0.0f }; postOrInline(c);
    }
    req->send(204);
  });

  // Set BPM (?b=0.5..60)
  server.on("/api/bpm", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("b")) {
      float b = req->getParam("b")->value().toFloat();
      Cmd c{ CMD_SET_BPM, 0, b }; postOrInline(c);
    }
    req->send(204);
  });

  // SSE (clients subscribe here)
  sse.onConnect([](AsyncEventSourceClient* client){
    if (client->lastId()) {
      Serial.printf("[WEB] SSE client reconnected, id: %u\n", client->lastId());
    } else {
      Serial.println("[WEB] SSE client connected");
    }
    client->send(": hello\n\n");
  });
  server.addHandler(&sse);

  // Allow simple cross-origin fetch from phones if needed
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();

  // Start the ~30 Hz streamer on Core 0
  xTaskCreatePinnedToCore(sse_task, "sse", 4096, nullptr, 3, nullptr, WEB_CORE);
}
