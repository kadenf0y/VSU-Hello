// web_cal.cpp
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "cal_store.h"

// Forward: your global server
extern AsyncWebServer server;

// Replace with your own ADC/raw getters:
extern float readAtrRawOnce();   // returns current raw sensor reading (unscaled)
extern float readVentRawOnce();
extern int   getValveDir();      // -1,0,+1
extern void  setValveDir(int dir);
extern void  setPwmRaw(uint8_t pwm);

// ---------- Serve the page ----------
static const char CAL_HTML[] PROGMEM = R"HTML(
<!doctype html><meta charset="utf-8">
<title>Calibration</title>
<style>
 body{font:14px system-ui;margin:12px;color:#e6edf3;background:#0b0f13}
 .row{display:flex;gap:8px;align-items:center;margin:6px 0}
 input,select,button{font:14px;padding:6px 8px;border-radius:6px;border:1px solid #334}
 table{width:100%;border-collapse:collapse;margin-top:10px}
 th,td{border:1px solid #334;padding:6px 8px;text-align:left}
 .muted{color:#9aa7b2}
 .box{border:1px solid #334;border-radius:8px;padding:10px;margin:8px 0}
 .cols{display:grid;grid-template-columns:1fr 1fr;gap:10px}
</style>
<div class="cols">
  <div class="box">
    <h3>Manual control</h3>
    <div class="row">
      <label>Valve:</label>
      <button onclick="setValve(-1)">REV (-1)</button>
      <button onclick="setValve(0)">NEUT (0)</button>
      <button onclick="setValve(1)">FWD (+1)</button>
      <span id="valveState" class="muted"></span>
    </div>
    <div class="row">
      <label>PWM (0..255):</label>
      <input id="pwm" type="number" min="0" max="255" value="0" style="width:90px">
      <button onclick="setPwm()">Apply</button>
      <span id="pwmState" class="muted"></span>
    </div>
  </div>

  <div class="box">
    <h3>Capture point</h3>
    <div class="row">
      <label>Channel:</label>
      <select id="ch">
        <option value="atr">Atrium</option>
        <option value="vent">Ventricle</option>
      </select>
      <label>Actual pressure (mmHg):</label>
      <input id="actual" type="number" step="0.1" value="0" style="width:110px">
      <label>Avg N:</label>
      <input id="navg" type="number" min="1" max="200" value="25" style="width:70px">
      <button onclick="capture()">Capture</button>
    </div>
    <div class="row muted" id="lastRaw"></div>
  </div>
</div>

<div class="box">
  <h3>Data</h3>
  <div class="row">
    <button onclick="fit()">Fit</button>
    <button onclick="clearCh()">Clear Channel</button>
    <button onclick="exportCSV()">Export CSV</button>
    <span id="fitOut" class="muted"></span>
  </div>
  <table id="tbl">
    <thead><tr><th>#</th><th>Channel</th><th>Raw</th><th>Actual (mmHg)</th><th>ms</th></tr></thead>
    <tbody></tbody>
  </table>
</div>

<script>
async function setValve(dir){
  await fetch('/api/valve?dir='+dir, {method:'POST'});
  document.getElementById('valveState').textContent = 'dir=' + dir;
}
async function setPwm(){
  const v = +document.getElementById('pwm').value||0;
  await fetch('/api/pwm?duty='+v, {method:'POST'});
  document.getElementById('pwmState').textContent = 'pwm='+v;
}

async function capture(){
  const ch = document.getElementById('ch').value;
  const actual = +document.getElementById('actual').value;
  const n = +document.getElementById('navg').value || 25;
  const r = await fetch(`/api/cal/capture?ch=${ch}&actual=${actual}&n=${n}`, {method:'POST'});
  const js = await r.json();
  document.getElementById('lastRaw').textContent = `raw=${js.raw.toFixed(3)} (avg of ${js.n} samples)`;
  loadList();
}

async function fit(){
  const ch = document.getElementById('ch').value;
  const r = await fetch(`/api/cal/fit?ch=${ch}`);
  const js = await r.json();
  document.getElementById('fitOut').textContent =
    `n=${js.n} slope=${js.slope.toFixed(6)} offset=${js.offset.toFixed(3)} r²=${js.r2.toFixed(4)}`;
}

async function clearCh(){
  const ch = document.getElementById('ch').value;
  await fetch(`/api/cal/clear?ch=${ch}`, {method:'POST'});
  loadList(); document.getElementById('fitOut').textContent='';
}

async function loadList(){
  const r = await fetch('/api/cal/list'); const js = await r.json();
  const tb = document.querySelector('#tbl tbody'); tb.innerHTML='';
  const rows = [];
  let i=1;
  for(const p of js.atr){ rows.push(row(i++,'atr', p)); }
  for(const p of js.vent){ rows.push(row(i++,'vent', p)); }
  tb.replaceChildren(...rows);
}
function row(i, ch, p){
  const tr = document.createElement('tr');
  tr.innerHTML = `<td>${i}</td><td>${ch}</td><td>${p.raw.toFixed(3)}</td><td>${p.actual.toFixed(3)}</td><td>${p.ms}</td>`;
  return tr;
}
async function exportCSV(){
  const r = await fetch('/api/cal/list'); const js = await r.json();
  const lines = ['channel,raw,actual,ms'];
  for(const p of js.atr) lines.push(`atr,${p.raw},${p.actual},${p.ms}`);
  for(const p of js.vent) lines.push(`vent,${p.raw},${p.actual},${p.ms}`);
  const blob = new Blob([lines.join('\\n')], {type:'text/csv'});
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = 'calibration.csv'; a.click();
  URL.revokeObjectURL(url);
}

loadList();
</script>
)HTML";

void web_cal_register_routes(){
  // Page
  server.on("/cal", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", CAL_HTML);
  });

  // Manual controls
  server.on("/api/pwm", HTTP_POST, [](AsyncWebServerRequest *req){
    int duty = req->getParam("duty", true)->value().toInt();
    duty = constrain(duty, 0, 255);
    setPwmRaw((uint8_t)duty);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/valve", HTTP_POST, [](AsyncWebServerRequest *req){
    int dir = req->getParam("dir", true)->value().toInt();  // -1,0,1
    dir = (dir>0)?1:(dir<0?-1:0);
    setValveDir(dir);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // Capture point: ch=atr|vent&actual=XX[.&n=avgSamples]
  server.on("/api/cal/capture", HTTP_POST, [](AsyncWebServerRequest *req){
    String ch = req->getParam("ch", true)->value();
    float actual = req->getParam("actual", true)->value().toFloat();
    int n = req->hasParam("n", true) ? req->getParam("n", true)->value().toInt() : 25;
    n = constrain(n, 1, 200);

    // average n samples of RAW sensor
    double acc=0;
    for(int i=0;i<n;i++){
      float raw = (ch=="atr") ? readAtrRawOnce() : readVentRawOnce();
      acc += raw;
      delay(2); // tiny spacing
    }
    float rawAvg = (float)(acc/n);

    gCal.add(ch, rawAvg, actual);

    char buf[96];
    snprintf(buf, sizeof(buf),
      "{\"ok\":true,\"ch\":\"%s\",\"raw\":%.6f,\"actual\":%.6f,\"n\":%d}",
      ch.c_str(), rawAvg, actual, n);
    req->send(200, "application/json", buf);
  });

  server.on("/api/cal/clear", HTTP_POST, [](AsyncWebServerRequest *req){
    String ch = req->getParam("ch", true)->value();
    gCal.clear(ch);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/cal/list", HTTP_GET, [](AsyncWebServerRequest *req){
    String out = "{\"atr\":[";
    for (size_t i=0;i<gCal.atr.size();i++){
      auto &p = gCal.atr[i];
      out += String("{\"raw\":")+p.raw+",\"actual\":"+p.actual+",\"ms\":"+p.ms+"}";
      if (i+1<gCal.atr.size()) out += ",";
    }
    out += "],\"vent\":[";
    for (size_t i=0;i<gCal.vent.size();i++){
      auto &p = gCal.vent[i];
      out += String("{\"raw\":")+p.raw+",\"actual\":"+p.actual+",\"ms\":"+p.ms+"}";
      if (i+1<gCal.vent.size()) out += ",";
    }
    out += "]}";
    req->send(200, "application/json", out);
  });

  server.on("/api/cal/fit", HTTP_GET, [](AsyncWebServerRequest *req){
    String ch = req->getParam("ch", true)->value();
    CalFit f = gCal.fit(ch);
    char buf[128];
    snprintf(buf, sizeof(buf),
      "{\"n\":%d,\"slope\":%.8f,\"offset\":%.6f,\"r2\":%.6f}",
      f.n, f.slope, f.offset, f.r2);
    req->send(200, "application/json", buf);
  });
}
