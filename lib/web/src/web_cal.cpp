// web_cal.cpp
#include "web_cal.h"
#include <vector>

// ----------------- Minimal in-memory store -----------------
struct CalPoint { float raw; float actual; uint32_t ms; };
struct CalFit   { float slope=1.f, offset=0.f, r2=0.f; int n=0; };

static std::vector<CalPoint> sAtr, sVent;

// Hooks provided by the app
static WebCalHooks H = {nullptr,nullptr,nullptr,nullptr,nullptr};

void web_cal_set_hooks(const WebCalHooks& hooks){ H = hooks; }

// Simple least-squares fit: actual ≈ slope*raw + offset
static CalFit fit(const std::vector<CalPoint>& v){
  CalFit f; f.n = (int)v.size();
  if (v.size() < 2) return f;
  double sx=0, sy=0, sxx=0, sxy=0;
  for (auto &p: v){ sx+=p.raw; sy+=p.actual; sxx+=p.raw*p.raw; sxy+=p.raw*p.actual; }
  const double n = (double)v.size();
  const double denom = (n*sxx - sx*sx);
  if (denom == 0) return f;
  f.slope  = (float)((n*sxy - sx*sy) / denom);
  f.offset = (float)((sy - f.slope*sx) / n);

  // r^2
  double meanY = sy/n, ssTot=0, ssRes=0;
  for (auto &p: v){
    double yhat = f.slope*p.raw + f.offset;
    ssRes += (p.actual - yhat)*(p.actual - yhat);
    ssTot += (p.actual - meanY)*(p.actual - meanY);
  }
  f.r2 = ssTot>0 ? (float)(1.0 - ssRes/ssTot) : 0.f;
  return f;
}

// --------------- Tiny HTML page (embedded) ----------------
static const char CAL_HTML[] PROGMEM = R"HTML(
<!doctype html><meta charset="utf-8">
<title>Calibration</title>
<style>
 body{font:14px system-ui;margin:12px;color:#e6edf3;background:#0b0f13}
 .row{display:flex;gap:8px;align-items:center;margin:6px 0}
 input,select,button{font:14px;padding:6px 8px;border-radius:6px;border:1px solid #334;background:#0f1317;color:#e6edf3}
 table{width:100%;border-collapse:collapse;margin-top:10px}
 th,td{border:1px solid #334;padding:6px 8px;text-align:left}
 .muted{color:#9aa7b2}
 .box{border:1px solid #334;border-radius:8px;padding:10px;margin:8px 0}
 .cols{display:grid;grid-template-columns:1fr 1fr;gap:10px}
 pre{background:#0f1317;border:1px solid #334;border-radius:8px;padding:10px;white-space:pre-wrap}
 code{font-family:ui-monospace, SFMono-Regular, Menlo, Consolas, monospace}
 .btn{cursor:pointer}
</style>

<div class="cols">
  <div class="box">
    <h3>Manual control</h3>
    <div class="row">
      <label>Valve (mode):</label>
      <button class="btn" onclick="setValve(0)">0 (Forward)</button>
      <button class="btn" onclick="setValve(1)">1 (Reverse)</button>
      <span id="valveState" class="muted"></span>
    </div>
    <div class="row">
      <label>PWM (0..255):</label>
      <input id="pwm" type="number" min="0" max="255" value="0" style="width:90px">
      <button class="btn" onclick="applyPwm()">Apply</button>
      <span id="pwmState" class="muted"></span>
    </div>
    <div class="row">
      <button id="pp" class="btn" onclick="toggleRun()">Play</button>
      <span class="muted">(Play/Pause)</span>
    </div>
  </div>

  <div class="box">
    <h3>Capture point</h3>
    <div class="row">
      <label>Channel:</label>
      <select id="ch">
        <option value="atr">Atrium</option>
        <option value="vent">Ventricle</option>
        <option value="both">Both</option>
      </select>
      <label>Actual (mmHg):</label>
      <input id="actual" type="number" step="0.1" value="0" style="width:110px">
      <label>Avg N:</label>
      <input id="navg" type="number" min="1" max="200" value="25" style="width:70px">
      <button class="btn" onclick="capture()">Capture</button>
    </div>
    <div class="row muted" id="lastRaw"></div>
  </div>
</div>

<div class="box">
  <h3>Live data (/stream)</h3>
  <pre id="live"><code>connecting…</code></pre>
</div>

<div class="box">
  <h3>Data</h3>
  <div class="row">
    <button class="btn" onclick="fit()">Fit</button>
    <button class="btn" onclick="clearCh()">Clear Channel</button>
    <button class="btn" onclick="exportCSV()">Export CSV</button>
    <span id="fitOut" class="muted"></span>
  </div>
  <table id="tbl">
    <thead><tr><th>#</th><th>Channel</th><th>Raw</th><th>Actual (mmHg)</th><th>ms</th></tr></thead>
    <tbody></tbody>
  </table>
</div>

<script>
// --------- State from SSE ----------
let lastPaused = true;

(function(){
  const pre = document.getElementById('live');
  const pp  = document.getElementById('pp');
  try{
    const es = new EventSource('/stream');
    es.onopen = ()=>{ pre.textContent = 'connected…'; };
    es.onerror = ()=>{ pre.textContent = 'SSE error'; };
    es.onmessage = (ev)=>{
      try{
        const d = JSON.parse(ev.data);
        lastPaused = !!d.paused;
        pp.textContent = lastPaused ? 'Play' : 'Pause';
        pre.textContent =
`paused=${d.paused}  mode=${d.mode}  powerPct=${d.powerPct}
pwm=${d.pwm}  valve=${d.valve}  bpm=${d.bpm}
vent_mmHg=${d.vent_mmHg}  atr_mmHg=${d.atr_mmHg}  flow_ml_min=${d.flow_ml_min}
loopMs=${d.loopMs}`;
      }catch(e){}
    };
  }catch(e){
    pre.textContent = 'SSE not supported';
  }
})();

// --------- Manual control wired to your existing endpoints ---------
// Valve buttons set MODE directly: /api/mode?m=0 or 1
async function setValve(m){
  await fetch(`/api/mode?m=${m}`).catch(()=>{});
  document.getElementById('valveState').textContent = 'mode=' + m;
}

// PWM input maps to power% 10..100 via /api/power?pct=..., and controls pause:
//  - If v==0: ensure system is paused
//  - If v>0:  set power% and ensure system is running
async function applyPwm(){
  const v = Math.max(0, Math.min(255, +document.getElementById('pwm').value||0));
  const pwmLabel = `pwm=${v}`;
  if (v === 0){
    // Pause if currently running
    if (!lastPaused) await fetch('/api/toggle').catch(()=>{});
    document.getElementById('pwmState').textContent = `${pwmLabel} → paused`;
    return;
  }
  // Map 0..255 → 10..100 (respect device floor)
  const pct = Math.max(10, Math.min(100, Math.round((v/255)*100)));
  await fetch(`/api/power?pct=${pct}`).catch(()=>{});
  if (lastPaused) await fetch('/api/toggle').catch(()=>{});
  document.getElementById('pwmState').textContent = `${pwmLabel} → powerPct=${pct}`;
}

// Explicit Play/Pause button
async function toggleRun(){
  await fetch('/api/toggle').catch(()=>{});
  // UI will update on next SSE tick
}

// --------- Capture (Atr / Vent / Both) ----------
async function capture(){
  const ch = document.getElementById('ch').value;
  const actual = document.getElementById('actual').value;
  const n = document.getElementById('navg').value || 25;

  const headers = {'Content-Type':'application/x-www-form-urlencoded'};
  const bodyStr = (c)=> new URLSearchParams({ch:c, actual, n});

  const results = [];
  if (ch === 'both'){
    const r1 = await fetch('/api/cal/capture', {method:'POST', headers, body: bodyStr('atr')});
    const j1 = r1.ok ? await r1.json() : null;
    const r2 = await fetch('/api/cal/capture', {method:'POST', headers, body: bodyStr('vent')});
    const j2 = r2.ok ? await r2.json() : null;
    if (!j1 || !j2) { alert('Capture failed'); return; }
    results.push(j1, j2);
  }else{
    const r = await fetch('/api/cal/capture', {method:'POST', headers, body: bodyStr(ch)});
    if (!r.ok){ alert('Capture failed'); return; }
    results.push(await r.json());
  }

  // Show last raw(s)
  const text = results.map(j=>`${j.ch}: raw=${j.raw.toFixed(3)} (N=${j.n})`).join('  |  ');
  document.getElementById('lastRaw').textContent = text;

  loadList();
}

// --------- Fit/Clear/List/Export (unchanged) ----------
async function fit(){
  const ch = document.getElementById('ch').value === 'both' ? 'atr' : document.getElementById('ch').value;
  const r = await fetch(`/api/cal/fit?ch=${encodeURIComponent(ch)}`);
  const js = await r.json();
  document.getElementById('fitOut').textContent =
    `n=${js.n} slope=${js.slope.toFixed(6)} offset=${js.offset.toFixed(3)} r²=${js.r2.toFixed(4)}`;
}
async function clearCh(){
  const sel = document.getElementById('ch').value;
  const targets = (sel === 'both') ? ['atr','vent'] : [sel];
  const headers = {'Content-Type':'application/x-www-form-urlencoded'};
  for (const ch of targets){
    await fetch('/api/cal/clear', {method:'POST', headers, body:new URLSearchParams({ch})});
  }
  loadList(); document.getElementById('fitOut').textContent='';
}
async function loadList(){
  const r = await fetch('/api/cal/list'); const js = await r.json();
  const tb = document.querySelector('#tbl tbody'); tb.innerHTML='';
  let i=1;
  js.atr.forEach(p=> addRow(i++,'atr',p));
  js.vent.forEach(p=> addRow(i++,'vent',p));
}
function addRow(i,ch,p){
  const tr=document.createElement('tr');
  tr.innerHTML=`<td>${i}</td><td>${ch}</td><td>${p.raw.toFixed(3)}</td><td>${p.actual.toFixed(3)}</td><td>${p.ms}</td>`;
  document.querySelector('#tbl tbody').appendChild(tr);
}
async function exportCSV(){
  const r = await fetch('/api/cal/list'); const js = await r.json();
  const lines = ['channel,raw,actual,ms'];
  js.atr.forEach(p=>lines.push(`atr,${p.raw},${p.actual},${p.ms}`));
  js.vent.forEach(p=>lines.push(`vent,${p.raw},${p.actual},${p.ms}`));
  const blob = new Blob([lines.join('\\n')], {type:'text/csv'});
  const url = URL.createObjectURL(blob); const a=document.createElement('a');
  a.href=url; a.download='calibration.csv'; a.click(); URL.revokeObjectURL(url);
}

// Initial table
loadList();
</script>
)HTML";


// ----------------- Route registration -----------------
void web_cal_register_routes(AsyncWebServer& server){
  // Ensure hooks provided
  // (we won't hard-fail, but manual controls will be no-ops if not set)
  server.on("/cal", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", CAL_HTML);
  });

  // Manual controls
  server.on("/api/pwm", HTTP_POST, [](AsyncWebServerRequest *req){
    if (!H.setPwmRaw) { req->send(500, "application/json", "{\"ok\":false,\"err\":\"no hook\"}"); return; }
    int duty = req->getParam("duty", true)->value().toInt();
    duty = constrain(duty, 0, 255);
    H.setPwmRaw((uint8_t)duty);
    req->send(200, "application/json", "{\"ok\":true}");
  });

server.on("/api/valve", HTTP_POST, [](AsyncWebServerRequest *req){
  if (!H.setValveDir) { req->send(500, "application/json", "{\"ok\":false,\"err\":\"no hook\"}"); return; }
  int dir = 0;
  if (req->hasParam("dir", true)) dir = req->getParam("dir", true)->value().toInt();
  else if (req->hasParam("dir"))  dir = req->getParam("dir")->value().toInt();
  dir = (dir > 0) ? 1 : 0; // only 0 or 1
  H.setValveDir(dir);
  req->send(200, "application/json", "{\"ok\":true}");
});


  // Capture a calibration point (average N samples of RAW)
server.on("/api/cal/capture", HTTP_POST, [](AsyncWebServerRequest *req){
  // Allow from POST body OR querystring
  String ch;
  if      (req->hasParam("ch", true)) ch = req->getParam("ch", true)->value();
  else if (req->hasParam("ch"))       ch = req->getParam("ch")->value();
  else { req->send(400, "application/json", "{\"ok\":false,\"err\":\"missing ch\"}"); return; }

  float actual = 0;
  if      (req->hasParam("actual", true)) actual = req->getParam("actual", true)->value().toFloat();
  else if (req->hasParam("actual"))       actual = req->getParam("actual")->value().toFloat();
  else { req->send(400, "application/json", "{\"ok\":false,\"err\":\"missing actual\"}"); return; }

  int n = 25;
  if      (req->hasParam("n", true)) n = req->getParam("n", true)->value().toInt();
  else if (req->hasParam("n"))       n = req->getParam("n")->value().toInt();
  n = constrain(n, 1, 1000);

  float (*reader)() = nullptr;
  if (ch=="atr") reader = H.readAtrRawOnce;
  else if (ch=="vent") reader = H.readVentRawOnce;
  if (!reader){ req->send(500, "application/json", "{\"ok\":false,\"err\":\"no reader\"}"); return; }

  double acc=0;
  for(int i=0;i<n;i++){ acc += reader(); delay(2); }
  float rawAvg = (float)(acc/n);

  if (ch=="atr") sAtr.push_back({rawAvg, actual, millis()});
  else           sVent.push_back({rawAvg, actual, millis()});

  char buf[160];
  snprintf(buf, sizeof(buf),
    "{\"ok\":true,\"ch\":\"%s\",\"raw\":%.6f,\"actual\":%.6f,\"n\":%d}",
    ch.c_str(), rawAvg, actual, n);
  req->send(200, "application/json", buf);
});


  // Clear channel
  server.on("/api/cal/clear", HTTP_POST, [](AsyncWebServerRequest *req){
    if (!req->hasParam("ch", true)){
      req->send(400, "application/json", "{\"ok\":false,\"err\":\"missing ch\"}"); return;
    }
    String ch = req->getParam("ch", true)->value();
    if (ch=="atr") sAtr.clear(); else sVent.clear();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // List points
  server.on("/api/cal/list", HTTP_GET, [](AsyncWebServerRequest *req){
    String out = "{\"atr\":[";
    for (size_t i=0;i<sAtr.size();i++){
      auto &p = sAtr[i];
      out += String("{\"raw\":")+p.raw+",\"actual\":"+p.actual+",\"ms\":"+p.ms+"}";
      if (i+1<sAtr.size()) out += ",";
    }
    out += "],\"vent\":[";
    for (size_t i=0;i<sVent.size();i++){
      auto &p = sVent[i];
      out += String("{\"raw\":")+p.raw+",\"actual\":"+p.actual+",\"ms\":"+p.ms+"}";
      if (i+1<sVent.size()) out += ",";
    }
    out += "]}";
    req->send(200, "application/json", out);
  });

  // Fit result
  server.on("/api/cal/fit", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->hasParam("ch")){
      req->send(400, "application/json", "{\"ok\":false,\"err\":\"missing ch\"}"); return;
    }
    String ch = req->getParam("ch")->value();
    CalFit f = (ch=="atr") ? fit(sAtr) : fit(sVent);
    char buf[160];
    snprintf(buf, sizeof(buf),
      "{\"n\":%d,\"slope\":%.8f,\"offset\":%.6f,\"r2\":%.6f}",
      f.n, f.slope, f.offset, f.r2);
    req->send(200, "application/json", buf);
  });
}
