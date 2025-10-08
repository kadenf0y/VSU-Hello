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
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>VSU ESP32 Monitor</title>
<style>
  :root{
    --bg:#0f1317; --panel:#151a20; --ink:#e6edf3; --muted:#9aa7b2; --grid:#26303a;
    --cyan:#0ea5e9; --red:#ef4444; --amber:#f59e0b; --green:#22c55e; --violet:#a78bfa;
  }
  *{box-sizing:border-box}
  html,body{height:100%}
  body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,Segoe UI,Roboto,Arial}

  /* Top status bar */
  .top{display:flex;gap:10px;align-items:center;padding:8px 12px;background:#0c1014;border-bottom:1px solid var(--grid)}
  .pill{font-size:12px;color:var(--muted);border:1px solid var(--grid);border-radius:999px;padding:3px 10px}
  .pill b{color:var(--ink)}

  /* Main 3-column grid */
  .main{display:grid;gap:12px;padding:12px;grid-template-columns:1fr 120px 280px}
  .col{display:flex;flex-direction:column;gap:10px}
  .card{background:var(--panel);border:1px solid var(--grid);border-radius:12px;padding:10px}

  /* Left column: strip cards */
  .strip{padding:0;overflow:hidden}
  .stripHead{display:flex;justify-content:space-between;align-items:center;padding:6px 10px;color:var(--muted);font-size:12px;border-bottom:1px solid var(--grid);background:#0c1116}
  .canvasWrap{height:110px;background:#0b0f13}
  canvas{display:block;width:100%;height:100%}
  .legend{display:flex;gap:12px;align-items:center;font-size:12px;color:var(--muted);padding:6px 10px;border-top:1px solid var(--grid);background:#0c1116}
  .sw{width:16px;height:4px;border-radius:2px;background:var(--muted)}

  /* Middle column: live numerics aligned to rows */
  .bigNum{display:flex;align-items:center;justify-content:center;height:110px;background:#0b0f13;border:1px solid var(--grid);border-radius:10px;font-weight:800}
  .bigNum .val{font-size:28px}
  .bigNum .unit{font-size:12px;color:var(--muted);margin-left:6px}

  /* Right column: controls */
  .controls .group{border:1px solid var(--grid);border-radius:10px;padding:10px;background:#0b0f13}
  .controls h3{margin:0 0 8px 0;font-size:13px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
  .bp{display:flex;align-items:baseline;gap:10px}
  .bp .main{font-weight:900;font-size:44px;letter-spacing:.02em}
  .bp .unit{color:var(--muted);font-size:12px}
  .row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
  input[type="range"]{width:100%}
  select,button{background:#0f1317;color:var(--ink);border:1px solid var(--grid);border-radius:8px;padding:6px 10px;cursor:pointer}
  .seg{display:flex;border:1px solid var(--grid);border-radius:10px;overflow:hidden}
  .seg button{flex:1;border:0;padding:8px 10px;background:#0f1317;color:var(--muted)}
  .seg button.active{background:#142033;color:var(--ink)}
  .seg button:not(:last-child){border-right:1px solid var(--grid)}
  .play{width:100%;padding:12px 10px;border-radius:10px;font-weight:800}
  .play.run{background:#15361f;border-color:#214d2b}
  .play.pause{background:#3a1414;border-color:#612626}
  .hint{font-size:11px;color:var(--muted)}

  /* Responsive */
  @media (max-width: 900px){
    .main{grid-template-columns:1fr}
  }
</style>
</head>
<body>
  <!-- Top status bar -->
  <div class="top">
    <span class="pill">SSE: <b id="sse">INIT</b></span>
    <span class="pill">FPS: <b id="fps">—</b></span>
    <span class="pill">Hz: <b id="hz">—</b></span>
    <span class="pill">Loop: <b id="loop">—</b></span>
    <span class="pill">IP: <b id="ip">—</b></span>
  </div>

  <!-- Main layout -->
  <div class="main">
    <!-- LEFT: 5 strip charts -->
    <div class="col">
      <div class="card strip">
        <div class="stripHead"><span>Atrium Pressure</span><span>mmHg</span></div>
        <div class="canvasWrap"><canvas id="cv-atr"></canvas></div>
        <div class="legend"><span class="sw" style="background:#0ea5e9"></span> −5 … 205 mmHg</div>
      </div>
      <div class="card strip">
        <div class="stripHead"><span>Ventricle Pressure</span><span>mmHg</span></div>
        <div class="canvasWrap"><canvas id="cv-vent"></canvas></div>
        <div class="legend"><span class="sw" style="background:#ef4444"></span> −5 … 205 mmHg</div>
      </div>
      <div class="card strip">
        <div class="stripHead"><span>Flow</span><span>mL/min</span></div>
        <div class="canvasWrap"><canvas id="cv-flow"></canvas></div>
        <div class="legend"><span class="sw" style="background:#f59e0b"></span> −500 … 500 mL/min</div>
      </div>
      <div class="card strip">
        <div class="stripHead"><span>Valve Direction</span><span>0–100</span></div>
        <div class="canvasWrap"><canvas id="cv-valve"></canvas></div>
        <div class="legend"><span class="sw" style="background:#22c55e"></span> 0 (REV) … 100 (FWD)</div>
      </div>
      <div class="card strip">
        <div class="stripHead"><span>Pump Power</span><span>%</span></div>
        <div class="canvasWrap"><canvas id="cv-power"></canvas></div>
        <div class="legend"><span class="sw" style="background:#a78bfa"></span> 0 … 100 %</div>
      </div>
    </div>

    <!-- MIDDLE: live numerics -->
    <div class="col">
      <div class="bigNum"><span class="val" id="n-atr">—</span><span class="unit">mmHg</span></div>
      <div class="bigNum"><span class="val" id="n-vent">—</span><span class="unit">mmHg</span></div>
      <div class="bigNum"><span class="val" id="n-flow">—</span><span class="unit">mL/min</span></div>
      <div class="bigNum"><span class="val" id="n-valve">—</span><span class="unit">/100</span></div>
      <div class="bigNum"><span class="val" id="n-power">—</span><span class="unit">%</span></div>
    </div>

    <!-- RIGHT: controls -->
    <div class="col controls">
      <div class="group">
        <h3>Blood Pressure</h3>
        <div class="bp">
          <span class="main" id="bp">—/—</span>
          <span class="unit">mmHg</span>
        </div>
        <div class="hint">SYS/DIA from last 5 s (Ventricle)</div>
      </div>

      <div class="group">
        <h3>Heartbeat</h3>
        <div class="row" style="margin-bottom:6px">
          <span class="pill">BPM: <b id="bpmLabel">—</b></span>
        </div>
        <input id="rngBpm" type="range" min="0.5" max="60" step="0.1">
        <div class="hint">Stops: 0.5, 1, 2, 5, 10, 20, 40, 60</div>
      </div>

      <div class="group">
        <h3>Pump Power</h3>
        <div class="row" style="margin-bottom:6px">
          <span class="pill">Power: <b id="powerLabel">—%</b></span>
        </div>
        <input id="rngPower" type="range" min="10" max="100" step="1">
      </div>

      <div class="group">
        <h3>Mode</h3>
        <div class="seg" id="modeSeg">
          <button data-m="0">Forward</button>
          <button data-m="1">Reverse</button>
          <button data-m="2">Beat</button>
        </div>
      </div>

      <div class="group">
        <h3>Run</h3>
        <button id="btnPause" class="play pause">Play / Pause</button>
      </div>

      <div class="group">
        <h3>Shortcuts</h3>
        <div class="hint">Space: Play/Pause • 1/2/3: Mode • ←/→: Power −/+10 • ↑/↓: BPM stops</div>
      </div>
    </div>
  </div>

<script>
/* -------------------- Status bar basics -------------------- */
const $ = (id)=>document.getElementById(id);
$('ip').textContent = location.host || '192.168.4.1';

/* -------------------- Canvas helpers -------------------- */
function fitCanvas(canvas, ctx){
  const dpr = window.devicePixelRatio || 1;
  const cssW = Math.max(1, canvas.clientWidth);
  const cssH = Math.max(1, canvas.clientHeight);
  const W = Math.floor(cssW * dpr), H = Math.floor(cssH * dpr);
  if (canvas.width !== W || canvas.height !== H){
    canvas.width = W; canvas.height = H;
    ctx.setTransform(dpr,0,0,dpr,0,0); // draw using CSS pixels
  }
}
function drawGrid(ctx, w, h, v=5, hlines=4){
  ctx.save();
  ctx.lineWidth = 1;
  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--grid').trim() || '#26303a';
  ctx.beginPath();
  const gx = w / v, gy = h / hlines;
  for(let x=0; x<=w; x+=gx){ ctx.moveTo(x+0.5,0); ctx.lineTo(x+0.5,h); }
  for(let y=0; y<=h; y+=gy){ ctx.moveTo(0,y+0.5); ctx.lineTo(w,y+0.5); }
  ctx.stroke();
  ctx.restore();
}

/* -------------------- Strip chart factory -------------------- */
const WINDOW_SEC = 5.0;    // 5-second sweep
const CAP = 150;           // ~30 Hz * 5 s

function makeStrip(canvasId, {min,max,color}){
  const cv = $(canvasId);
  const ctx = cv.getContext('2d');
  const buf = []; // {t,v}
  function push(v){
    const now = performance.now()/1000;
    buf.push({t:now, v});
    while(buf.length && now - buf[0].t > WINDOW_SEC) buf.shift();
    render();
  }
  function render(){
    fitCanvas(cv, ctx);
    const W = cv.clientWidth, H = cv.clientHeight;
    ctx.clearRect(0,0,W,H);
    drawGrid(ctx, W, H, 5, 4);
    if (buf.length < 2) return;

    const tNow = performance.now()/1000;
    const Y = (val)=> H - ( (val - min) / (max - min) ) * H;
    const X = (t)=> ((t % WINDOW_SEC) / WINDOW_SEC) * W;

    ctx.lineWidth = 2;
    ctx.strokeStyle = color;

    for (let i=1;i<buf.length;i++){
      const a = buf[i-1], b = buf[i];
      const xa = X(a.t), xb = X(b.t);
      // skip wrap jump
      if (xb < xa && (xa - xb) > (W*0.2)) continue;

      // Age-based alpha (quadratic)
      const age = tNow - b.t;
      let alpha = 1 - (age / WINDOW_SEC);
      if (alpha <= 0) continue;
      alpha *= alpha;
      ctx.globalAlpha = alpha;

      ctx.beginPath();
      ctx.moveTo(xa, Y(a.v));
      ctx.lineTo(xb, Y(b.v));
      ctx.stroke();
    }
    ctx.globalAlpha = 1;
  }
  window.addEventListener('resize', render);
  return {push, render, buf};
}

/* -------------------- Build strips -------------------- */
const stripAtr   = makeStrip('cv-atr',   {min:-5,   max:205,  color:'#0ea5e9'});
const stripVent  = makeStrip('cv-vent',  {min:-5,   max:205,  color:'#ef4444'});
const stripFlow  = makeStrip('cv-flow',  {min:-500, max:500,  color:'#f59e0b'});
const stripValve = makeStrip('cv-valve', {min:0,    max:100,  color:'#22c55e'});
const stripPower = makeStrip('cv-power', {min:0,    max:100,  color:'#a78bfa'});

/* -------------------- Live numerics + BP -------------------- */
function setNum(id, v, fixed=1){ $(id).textContent = (v==null||isNaN(v))?'—':Number(v).toFixed(fixed); }
function setText(id, s){ $(id).textContent = s; }

function computeBP(){
  // SYS/DIA = max/min of Ventricle over last 5s
  const b = stripVent.buf;
  if (!b.length){ setText('bp','—/—'); return; }
  let hi = -1e9, lo = 1e9;
  for (const p of b){ if (p.v>hi) hi=p.v; if (p.v<lo) lo=p.v; }
  setText('bp', `${Math.round(hi)}/${Math.round(lo)}`);
}

/* -------------------- Controls wiring -------------------- */
const bpmStops = [0.5,1,2,5,10,20,40,60];
function snapBpm(x){
  let best = bpmStops[0], d=1e9;
  for (const s of bpmStops){ const dd=Math.abs(s-x); if (dd<d){ d=dd; best=s; } }
  return best;
}
function setModeButtons(m){
  document.querySelectorAll('#modeSeg button').forEach(b=>{
    b.classList.toggle('active', Number(b.dataset.m)===m);
  });
}
function send(url){ fetch(url).catch(()=>{}); }

$('btnPause').onclick = ()=> send('/api/toggle');

document.querySelectorAll('#modeSeg button').forEach(btn=>{
  btn.addEventListener('click', ()=>{
    const m = Number(btn.dataset.m)||0;
    setModeButtons(m);
    send('/api/mode?m='+m);
  });
});

$('rngPower').addEventListener('input', ()=>{
  $('powerLabel').textContent = $('rngPower').value+'%';
});
$('rngPower').addEventListener('change', ()=>{
  const v = Number($('rngPower').value)|0;
  send('/api/power?pct='+v);
});

$('rngBpm').addEventListener('input', ()=>{
  const raw = Number($('rngBpm').value);
  const s = snapBpm(raw);
  $('rngBpm').value = String(s);
  $('bpmLabel').textContent = s.toFixed(1);
});
$('rngBpm').addEventListener('change', ()=>{
  const v = Number($('rngBpm').value);
  send('/api/bpm?b='+v);
});

/* Keyboard shortcuts */
window.addEventListener('keydown', (e)=>{
  if (e.code==='Space'){ e.preventDefault(); send('/api/toggle'); return; }
  if (e.key==='1'){ send('/api/mode?m=0'); return; }
  if (e.key==='2'){ send('/api/mode?m=1'); return; }
  if (e.key==='3'){ send('/api/mode?m=2'); return; }

  if (e.key==='ArrowLeft' || e.key==='ArrowRight'){
    let cur = Number($('rngPower').value)|0;
    let next = cur + (e.key==='ArrowRight'? +10 : -10);
    if (next<10) next=10; if (next>100) next=100;
    $('rngPower').value = String(next);
    $('powerLabel').textContent = next+'%';
    send('/api/power?pct='+next);
  }
  if (e.key==='ArrowUp' || e.key==='ArrowDown'){
    const cur = Number($('rngBpm').value);
    let idx = bpmStops.indexOf(snapBpm(cur));
    idx += (e.key==='ArrowUp'? +1 : -1);
    if (idx<0) idx=0; if (idx>=bpmStops.length) idx=bpmStops.length-1;
    const v = bpmStops[idx];
    $('rngBpm').value = String(v);
    $('bpmLabel').textContent = v.toFixed(1);
    send('/api/bpm?b='+v);
  }
});

/* -------------------- SSE hookup -------------------- */
const es = new EventSource('/stream');
let lastFrame = performance.now(), emaFPS = 0;
es.onopen  = ()=>{ $('sse').textContent='OPEN'; };
es.onerror = ()=>{ $('sse').textContent='ERROR'; };
es.onmessage = (ev)=>{
  // FPS calc (browser-side)
  const now = performance.now(), dt = now - lastFrame; lastFrame = now;
  const fps = 1000/Math.max(1,dt); emaFPS = emaFPS ? (emaFPS*0.9 + fps*0.1) : fps;
  $('fps').textContent = emaFPS.toFixed(1);

  try{
    const d = JSON.parse(ev.data);

    // Push values into strips
    const valvePct = d.valve ? 100 : 0;
    stripAtr.push( Number(d.atr_mmHg)   || 0 );
    stripVent.push(Number(d.vent_mmHg)  || 0 );
    stripFlow.push(Number(d.flow_ml_min)|| 0 );
    stripValve.push(valvePct);
    stripPower.push(Number(d.powerPct)  || 0 );

    // Middle numerics
    setNum('n-atr',   d.atr_mmHg,1);
    setNum('n-vent',  d.vent_mmHg,1);
    setNum('n-flow',  d.flow_ml_min,1);
    $('n-valve').textContent = valvePct.toFixed(0);
    setNum('n-power', d.powerPct,0);

    // BP from ventricle buffer
    computeBP();

    // Status pills
    const loopMs = Number(d.loopMs)||0;
    if (loopMs>0){
      $('hz').textContent = (1000/loopMs).toFixed(1);
      $('loop').textContent = loopMs.toFixed(2)+' ms';
    }

    // Reflect controls from device state
    setModeButtons(Number(d.mode)||0);

    const p = Number(d.powerPct)||0;
    if (Math.abs(Number($('rngPower').value)-p) >= 1){
      $('rngPower').value = String(p);
      $('powerLabel').textContent = p+'%';
    }
    const b = Number(d.bpm)||0;
    const s = snapBpm(b);
    if (Math.abs(Number($('rngBpm').value)-s) > 0.0001){
      $('rngBpm').value = String(s);
      $('bpmLabel').textContent = s.toFixed(1);
    }

    const paused = !!d.paused;
    const btn = $('btnPause');
    btn.classList.toggle('run',  !paused);
    btn.classList.toggle('pause', paused);
    btn.textContent = paused ? 'Play' : 'Pause';

  }catch(e){}
};

// First paint on load/resize
window.addEventListener('load', ()=>{
  stripAtr.render(); stripVent.render(); stripFlow.render(); stripValve.render(); stripPower.render();
});
</script>
</body>
</html>
)HTML";

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
