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
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>VSU ESP32 Monitor</title>
<style>
  :root{
    --bg:#0f1317; --panel:#151a20; --ink:#e6edf3; --muted:#9aa7b2; --grid:#26303a;
    --cyan:#0ea5e9; --red:#ef4444; --amber:#f59e0b; --green:#22c55e; --violet:#a78bfa;
    --gap:10px;
  }
  *{box-sizing:border-box}
  html,body{height:100%; overflow:hidden}
  body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,Segoe UI,Roboto,Arial}

  /* Top status bar */
  .top{display:flex;gap:8px;align-items:center;padding:6px 10px;background:#0c1014;border-bottom:1px solid var(--grid)}
  .pill{font-size:12px;color:var(--muted);border:1px solid var(--grid);border-radius:999px;padding:2px 8px;white-space:nowrap}
  .pill b{color:var(--ink)}

  /* Main 3-col grid */
.main{
display:grid; gap:12px; padding:10px;
grid-template-columns:minmax(320px,1fr) minmax(96px,120px) minmax(260px,300px);
height:calc(100vh - 40px);  /* top bar assumed 40px */
max-width:100vw;
}

.leftgrid, .midgrid, .rightgrid{
display:grid;
grid-template-rows: repeat(5, minmax(60px, 1fr));
gap: var(--gap);
min-height:0;
}

  .canvasWrap{ height:100%; min-height:0; } 

  /* Strips */
  .strip{
    display:grid;grid-template-rows:auto 1fr auto;gap:0;
    background:var(--panel);border:1px solid var(--grid);border-radius:12px;overflow:hidden;min-height:0;
  }
  .stripHead{display:flex;justify-content:space-between;align-items:center;
    padding:4px 8px;color:var(--muted);font-size:11px;border-bottom:1px solid var(--grid);background:#0c1116}
  .legend{display:flex;gap:10px;align-items:center;font-size:11px;color:var(--muted);
    padding:4px 8px;border-top:1px solid var(--grid);background:#0c1116}
  .sw{width:14px;height:3px;border-radius:2px;background:var(--muted)}
  canvas{display:block;width:100%;height:100%;background:#0b0f13}

  /* Middle numerics */
  .bigNum{
    display:flex;align-items:center;justify-content:center;height:100%;
    background:#0b0f13;border:1px solid var(--grid);border-radius:10px;min-height:0
  }
  .numStack{display:flex;flex-direction:column;align-items:center;line-height:1}
  .numStack .val{font-weight:900;letter-spacing:.02em;font-size:clamp(18px,3.0vh,28px)}
  .numStack .unit{font-size:11px;color:var(--muted);margin-top:4px}
  .t-cyan .val{color:var(--cyan)}
  .t-red .val{color:var(--red)}
  .t-amber .val{color:var(--amber)}
  .t-green .val{color:var(--green)}
  .t-violet .val{color:var(--violet)}

  /* Right column cards */
  .group{
    height:100%;
    display:flex;flex-direction:column;justify-content:center;align-items:stretch;
    border:1px solid var(--grid);border-radius:10px;background:#0b0f13;padding:8px;min-height:0; overflow:hidden;
  }
  .group h3{margin:0 0 6px 0;font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
  .hint{font-size:11px;color:var(--muted)}
  .row{display:flex;gap:8px;align-items:center}

  /* BP compact */
  .bp{display:inline-flex;align-items:baseline;gap:8px;white-space:nowrap}
  .bp .main{font-weight:900;font-size:34px;line-height:1;letter-spacing:.02em}
  .bp .unit{color:var(--muted);font-size:12px}

  /* Play/Pause (corrected colors): green when text says Play (paused), red when text says Pause (running) */
  .pp{width:100%;padding:12px 10px;border-radius:10px;font-weight:800;border:1px solid var(--grid);cursor:pointer;transition:filter .12s}
  .pp.go{   background:#1f8f44;border-color:#2aa456;box-shadow:0 0 0 1px rgba(42,164,86,.25) inset} /* shows "Play" */
  .pp.stop{ background:#a93636;border-color:#be4a4a;box-shadow:0 0 0 1px rgba(190,74,74,.25) inset} /* shows "Pause" */
  .pp:hover{filter:brightness(1.06)}
  .pp:disabled{opacity:.6;cursor:not-allowed}

  /* Segmented mode control */
  .seg{display:flex;border:1px solid var(--grid);border-radius:10px;overflow:hidden}
  .seg button{flex:1;border:0;padding:8px 10px;background:#0f1317;color:var(--muted);cursor:pointer}
  .seg button.active{background:#142033;color:var(--ink)}
  .seg button:not(:last-child){border-right:1px solid var(--grid)}

  /* Vertical steppers (editable, ±5) */
  .stepper{display:grid;grid-template-rows:auto 1fr auto;align-items:center;justify-items:center;gap:6px;min-height:0}
  .stepper .btn{width:100%;padding:8px;border:1px solid var(--grid);border-radius:8px;background:#0f1317;color:var(--ink);cursor:pointer}
  .stepper .btn:hover{background:#101823}
  .stepper .readout{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;width:100%;min-height:0;border:1px solid var(--grid);border-radius:10px;background:#0f1317;padding:8px}
  .stepper .val{font-weight:900;font-size:22px}
  .stepper .val[contenteditable="true"]{outline:none;border-bottom:1px dashed var(--grid);padding:2px 6px;border-radius:6px}
  .stepper .sub{font-size:11px;color:var(--muted);margin-top:2px}

  /* Last row: two steppers side-by-side */
  .twoCol{display:grid;grid-template-columns:1fr 1fr;gap:8px;height:100%}

  /* Valve direction arrow tile */
  .dirTile{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%}
  .dirTile .lblTop{font-size:12px;color:var(--muted);margin-bottom:4px}
  .dirTile .arrow{font-size:28px;line-height:1;color:var(--green)}
  .dirTile .lblBot{font-size:12px;color:var(--muted);margin-top:4px}

  /* Responsive */
  @media (max-width: 900px){ .main{grid-template-columns:1fr} }
  @media (max-height: 760px){
    .legend{padding:2px 8px;font-size:10px}
    .stripHead{padding:3px 8px}
    .bp .main{font-size:30px}
  }
</style>
</head>
<body>
  <!-- Top status bar -->
  <div class="top" id="topbar">
    <span class="pill">SSE: <b id="sse">INIT</b></span>
    <span class="pill">FPS: <b id="fps">—</b></span>
    <span class="pill">Hz: <b id="hz">—</b></span>
    <span class="pill">Loop: <b id="loop">—</b></span>
    <span class="pill">IP: <b id="ip">—</b></span>
  </div>

  <!-- Main layout -->
  <div class="main" id="main">
    <!-- LEFT: 5 strip charts -->
    <div class="leftgrid" id="leftgrid">
      <div class="strip">
        <div class="stripHead"><span>Atrium Pressure</span><span>mmHg</span></div>
        <div class="canvasWrap"><canvas id="cv-atr"></canvas></div>
        <div class="legend"><span class="sw" style="background:#0ea5e9"></span> −5 … 205 mmHg</div>
      </div>
      <div class="strip">
        <div class="stripHead"><span>Ventricle Pressure</span><span>mmHg</span></div>
        <div class="canvasWrap"><canvas id="cv-vent"></canvas></div>
        <div class="legend"><span class="sw" style="background:#ef4444"></span> −5 … 205 mmHg</div>
      </div>
      <div class="strip">
        <div class="stripHead"><span>Flow</span><span>mL/min</span></div>
        <div class="canvasWrap"><canvas id="cv-flow"></canvas></div>
        <div class="legend"><span class="sw" style="background:#f59e0b"></span> 0 … 750 mL/min</div>
      </div>
      <div class="strip">
        <div class="stripHead"><span>Valve Direction</span><span>−1…1</span></div>
        <div class="canvasWrap"><canvas id="cv-valve"></canvas></div>
        <div class="legend"><span class="sw" style="background:#22c55e"></span> -1 (REV) … 0 … +1 (FWD)</div>
      </div>
      <div class="strip">
        <div class="stripHead"><span>Pump (PWM raw)</span><span>counts</span></div>
        <div class="canvasWrap"><canvas id="cv-power"></canvas></div>
        <div class="legend"><span class="sw" style="background:#a78bfa"></span> PWM floor … 255</div>
      </div>

    </div>

    <!-- MIDDLE: 5 numeric tiles -->
    <div class="midgrid" id="midgrid">
      <div class="bigNum t-cyan"><div class="numStack"><span class="val" id="n-atr">—</span><span class="unit">mmHg</span></div></div>
      <div class="bigNum t-red"><div class="numStack"><span class="val" id="n-vent">—</span><span class="unit">mmHg</span></div></div>
      <div class="bigNum t-amber"><div class="numStack"><span class="val" id="n-flow">—</span><span class="unit">mL/min</span></div></div>

      <!-- Valve direction: Ventricle ↑ / Atrium ↓ with green arrow -->
      <div class="bigNum">
        <div class="dirTile">
          <div class="lblTop">Ventricle</div>
          <div class="arrow" id="n-valve-arrow">—</div>
          <div class="lblBot">Atrium</div>
        </div>
      </div>

      <!-- Pump numeric shows SET POWER % (10..100), not raw PWM -->
      <div class="bigNum t-violet"><div class="numStack"><span class="val" id="n-power">—</span><span class="unit">PWM</span></div></div>
    </div>

    <!-- RIGHT: 5-row controls grid -->
    <div class="rightgrid" id="rightgrid">
      <!-- Row 1: Blood Pressure -->
        <div class="group">
        <h3>Blood Pressure</h3>
        <div class="bigNum">
            <div class="numStack">
            <span class="val" id="bp">—/—</span>
            <span class="unit">mmHg</span>
            </div>
        </div>
        <div class="hint">SYS/DIA from last 5 s (Ventricle)</div>
        </div>

      <!-- Row 2: Play/Pause -->
      <div class="group">
        <h3>Run</h3>
        <button id="btnPause" class="pp go">Play</button>
      </div>

      <!-- Row 3: Control Scheme (hotkeys + calibration link) -->
      <div class="group">
        <h3>Control Scheme</h3>
        <div class="hint" style="margin-bottom:6px">
          Space = Play/Pause • 1/2/3 = Mode • ←/→ = Power ±1 • ↑/↓ = BPM ±1
        </div>
        <a class="row" href="/cal" style="text-decoration:none;color:#8ab4f8;background:#0f1a26;border:1px solid #1b3550;border-radius:8px;padding:8px;justify-content:center">
          ⚙️ Calibration &amp; Sensor Trim
        </a>
      </div>

      <!-- Row 4: Mode -->
      <div class="group">
        <h3>Mode</h3>
        <div class="seg" id="modeSeg">
          <button data-m="0">Forward</button>
          <button data-m="1">Reverse</button>
          <button data-m="2">Beat</button>
        </div>
      </div>

      <!-- Row 5: Power + BPM (two half-width steppers) -->
      <div class="group">
        <h3>Setpoints</h3>
        <div class="twoCol">
          <!-- Power (10..100, ±5) -->
          <div class="stepper" id="stpPower">
            <button class="btn" data-dir="up">▲</button>
            <div class="readout">
              <div class="val" id="powerLabel" contenteditable="true" spellcheck="false">—%</div>
              <div class="sub">Power 10–100%</div>
            </div>
            <button class="btn" data-dir="down">▼</button>
          </div>
          <!-- BPM (1..60, ±5) -->
          <div class="stepper" id="stpBpm">
            <button class="btn" data-dir="up">▲</button>
            <div class="readout">
              <div class="val" id="bpmLabel" contenteditable="true" spellcheck="false">—</div>
              <div class="sub">BPM 1–60</div>
            </div>
            <button class="btn" data-dir="down">▼</button>
          </div>
        </div>
      </div>
    </div>
  </div>

<script>
/* ---------- Helpers & layout ---------- */
const $ = (id)=>document.getElementById(id);
$('ip').textContent = location.host || '192.168.4.1';

/* Lock each column to 5 equal rows with identical pixel heights */
function fitLayout(){
  const top = document.getElementById('topbar');
  const main = document.getElementById('main');
  const left = document.getElementById('leftgrid');
  const mid  = document.getElementById('midgrid');
  const right= document.getElementById('rightgrid');

  const topH = top.getBoundingClientRect().height || 0;
  const availMainH = Math.max(300, window.innerHeight - topH - 10);
  main.style.height = availMainH + 'px';


}

/* ---------- Canvas helpers ---------- */
function fitCanvas(canvas, ctx){  // added this versions ---------------------------------------
  // Size the backing-store to exactly the CSS box; no DPR scaling.
  const cssW = Math.max(1, canvas.clientWidth|0);
  const cssH = Math.max(1, canvas.clientHeight|0);
  if (canvas.width !== cssW || canvas.height !== cssH){
    canvas.width = cssW;
    canvas.height = cssH;
  }
  // Ensure identity transform every frame.
  ctx.setTransform(1,0,0,1,0,0);
}

function drawGrid(ctx, w, h, v=5, hlines=4){
  ctx.save(); ctx.lineWidth=1;
  ctx.strokeStyle=getComputedStyle(document.documentElement).getPropertyValue('--grid').trim()||'#26303a';
  ctx.beginPath();
  const gx=w/v, gy=h/hlines;
  for(let x=0;x<=w;x+=gx){ ctx.moveTo(x+0.5,0); ctx.lineTo(x+0.5,h); }
  for(let y=0;y<=h;y+=gy){ ctx.moveTo(0,y+0.5); ctx.lineTo(w,y+0.5); }
  ctx.stroke(); ctx.restore();
}

/* ---------- Strip chart (fixed Y ranges) ---------- */
const WINDOW_SEC = 5.0;
const PWM_FLOOR = 165; /* must match firmware floor */
let lastPwm = PWM_FLOOR;

function makeStrip(canvasId, cfg){
  const cv = $(canvasId), ctx = cv.getContext('2d');

  // live state (editable later)
  const st = {
    min:  (cfg && cfg.min  != null) ? cfg.min  : 0,
    max:  (cfg && cfg.max  != null) ? cfg.max  : 1,
    color:(cfg && cfg.color) || '#fff',
    auto: !!(cfg && cfg.auto),
    pad:  (cfg && cfg.pad  != null) ? cfg.pad  : 0.10,
    softMin: (cfg && cfg.softMin != null) ? cfg.softMin : -Infinity,
    softMax: (cfg && cfg.softMax != null) ? cfg.softMax :  Infinity,
  };

  const buf = []; // {t,v}

  function setRange(min, max){
    st.min = Number(min);
    st.max = Number(max);
    st.auto = false;
    render();
  }
  function setAuto(on=true, pad=st.pad){
    st.auto = !!on; st.pad = pad;
    render();
  }

  function currentRange(){
    if (!st.auto || buf.length < 2) return {yMin: st.min, yMax: st.max};
    let lo = Infinity, hi = -Infinity;
    for (const p of buf){ const v = p.v; if (v < lo) lo = v; if (v > hi) hi = v; }
    if (!isFinite(lo) || !isFinite(hi) || lo === hi){
      return {yMin: st.min, yMax: st.max};
    }
    const span = hi - lo, pad = span * st.pad;
    let yMin = Math.max(st.softMin, lo - pad);
    let yMax = Math.min(st.softMax, hi + pad);
    if (yMax <= yMin) yMax = yMin + 1;
    return {yMin, yMax};
  }

  function push(v){
    const now = performance.now()/1000;
    buf.push({t: now, v});
    while (buf.length && now - buf[0].t > WINDOW_SEC) buf.shift();
    render();
  }

function render(){
  fitCanvas(cv, ctx);

  // Use the canvas’s backing-store size directly (CSS px, 1:1)
  const W = cv.width, H = cv.height;

  // Full clear
  ctx.clearRect(0,0,W,H);

  // Grid
  drawGrid(ctx, W, H, 5, 4);

  if (buf.length < 2) return;

  // FIXED RANGE (no autoscale if st.auto is false)
  const { yMin, yMax } = (!st.auto || buf.length < 2)
    ? { yMin: st.min, yMax: st.max }
    : currentRange(); // you can leave this branch; it won't be used when auto=false

  const tNow = performance.now()/1000;

  const Y = (val)=>{
    const vv = Math.max(yMin, Math.min(yMax, val));
    return H - ((vv - yMin) / (yMax - yMin)) * H;
  };
  const X = (t)=> ((t % WINDOW_SEC) / WINDOW_SEC) * W;

  ctx.lineWidth = 2;
  ctx.strokeStyle = st.color;

  for (let i=1; i<buf.length; i++){
    const a = buf[i-1], b = buf[i];
    const xa = X(a.t), xb = X(b.t);
    if (xb < xa && (xa - xb) > (W * 0.2)) continue; // skip modulo wrap
    let alpha = 1 - ((tNow - b.t) / WINDOW_SEC);
    if (alpha <= 0) continue; alpha *= alpha;
    ctx.globalAlpha = alpha;
    ctx.beginPath();
    ctx.moveTo(xa, Y(a.v));
    ctx.lineTo(xb, Y(b.v));
    ctx.stroke();
  }
  ctx.globalAlpha = 1;

  // Optional tiny overlay so you can SEE what the painter is using
  ctx.fillStyle = 'rgba(255,255,255,.6)';
  ctx.font = '10px system-ui';
  ctx.fillText(`Y=[${yMin}..${yMax}] H=${H}px`, 6, 12);
}


  addEventListener('resize', render);

  // return the strip with live setters + state
  return { push, render, buf, setRange, setAuto, state: st };
}

// Replace your strip constructors with these for now:
const stripAtr   = makeStrip('cv-atr',   {min: 0, max: 800, color:'#0ea5e9'});
const stripVent  = makeStrip('cv-vent',  {min: 0, max: 800, color:'#ef4444'});
const stripFlow  = makeStrip('cv-flow',  {min: -0.1, max: 750, color:'#f59e0b'});
const stripValve = makeStrip('cv-valve', {min:-0.1, max: 1.1, color:'#22c55e'});
const stripPower = makeStrip('cv-power', {min: -1, max: 256, color:'#a78bfa'});

/* ---------- Numerics & BP ---------- */
function setNum(id, v, fixed=1){ $(id).textContent=(v==null||isNaN(v))?'—':Number(v).toFixed(fixed); }
function setText(id, s){ $(id).textContent=s; }

function computeBP(){
  // Mode 2 (Beat):
  //  - While valve==0 → SYS = max(SYS, ventPressure)
  //  - While valve==1 → DIA = max(DIA, ventPressure)
  //  - On rising edge (0 → 1): publish "SYS/DIA", then reset SYS=DIA=0
  //
  // Modes 0/1: show "NA/NA" and reset accumulators.

  const s = computeBP._s || (computeBP._s = {
    prevDir: null,       // last binary valve dir (0 or 1)
    sysMax: 0,
    diaMax: 0,
    lastBPText: '—/—'
  });

  // Buffers (latest samples)
  const v = stripVent.buf.at(-1)?.v;   // ventricle pressure
  const valveRaw = stripValve.buf.at(-1)?.v; // could be -1..+1
  if (v == null || valveRaw == null){ setText('bp', s.lastBPText); return; }

  // Resolve current mode from UI (0=fwd,1=rev,2=beat)
  const activeBtn = document.querySelector('#modeSeg button.active');
  const mode = activeBtn ? (Number(activeBtn.dataset.m) || 0) : 0;

  if (mode !== 2){
    // Not beating → reset and show NA/NA
    s.prevDir = null;
    s.sysMax = 0;
    s.diaMax = 0;
    s.lastBPText = 'NA/NA';
    setText('bp', s.lastBPText);
    return;
  }

  // Binary valve state per your spec: treat <=0 as 0, >0 as 1
  const dir = (valveRaw > 0) ? 1 : 0;

  // Accumulate maxima in the current half-cycle
  if (dir === 0){
    if (v > s.sysMax) s.sysMax = v;
  }else{
    if (v > s.diaMax) s.diaMax = v;
  }

  // Detect rising edge: 0 → 1
  if (s.prevDir === 0 && dir === 1){
    // Publish and reset
    s.lastBPText = `${s.sysMax.toFixed(1)}/${s.diaMax.toFixed(1)}`;
    setText('bp', s.lastBPText);
    s.sysMax = 0;
    s.diaMax = 0;
  }else{
    // No edge this tick: keep last published value on screen
    setText('bp', s.lastBPText);
  }

  // Update edge tracker
  s.prevDir = (s.prevDir == null) ? dir : dir;
}

/* ---------- Controls ---------- */
let uiBpm = 10, uiPowerSet = 30;
let editingBpm = false, editingPow = false;
let ignoreSseUntil = 0;

function clamp(v, lo, hi){ return Math.min(hi, Math.max(lo, v)); }
function parseIntStrict(s){ const n=parseInt(String(s).trim().replace(/[^\d\-]/g,''),10); return isNaN(n)?null:n; }

function stepHold(buttonEl, fn){
  let rpt=null;
  const start=()=>{ fn(); rpt=setInterval(fn, 140); };
  const stop =()=>{ if(rpt){clearInterval(rpt); rpt=null;} };
  buttonEl.addEventListener('mousedown', start);
  buttonEl.addEventListener('touchstart', (e)=>{ e.preventDefault(); start(); }, {passive:false});
  ['mouseup','mouseleave','touchend','touchcancel'].forEach(ev=> buttonEl.addEventListener(ev, stop));
}

function powerApply(newVal){
  const v = clamp(newVal|0, 10, 100);
  uiPowerSet = v; $('powerLabel').textContent = v + '%';
  fetch('/api/power?pct='+v).catch(()=>{});
}
function bpmApply(newVal){
  const v = clamp(newVal|0, 1, 60);
  uiBpm = v; $('bpmLabel').textContent = String(v);
  fetch('/api/bpm?b='+v).catch(()=>{});
}

(function bindControls(){
  // Power (±5)
  stepHold(document.querySelector('#stpPower [data-dir="up"]'),   ()=> powerApply(uiPowerSet + 5));
  stepHold(document.querySelector('#stpPower [data-dir="down"]'), ()=> powerApply(uiPowerSet - 5));
  const pField = $('powerLabel');
  pField.addEventListener('focus', ()=>{ editingPow=true; });
  pField.addEventListener('keydown', (e)=>{ if(e.key==='Enter'){ e.preventDefault(); pField.blur(); }});
  pField.addEventListener('blur', ()=>{
    const n = parseIntStrict(pField.textContent);
    if (n==null){ pField.textContent = uiPowerSet + '%'; editingPow=false; return; }
    editingPow=false; powerApply(n);
  });

  // BPM (±5)
  stepHold(document.querySelector('#stpBpm [data-dir="up"]'),   ()=> bpmApply(uiBpm + 5));
  stepHold(document.querySelector('#stpBpm [data-dir="down"]'), ()=> bpmApply(uiBpm - 5));
  const bField = $('bpmLabel');
  bField.addEventListener('focus', ()=>{ editingBpm=true; });
  bField.addEventListener('keydown', (e)=>{ if(e.key==='Enter'){ e.preventDefault(); bField.blur(); }});
  bField.addEventListener('blur', ()=>{
    const n = parseIntStrict(bField.textContent);
    if (n==null){ bField.textContent = String(uiBpm); editingBpm=false; return; }
    editingBpm=false; bpmApply(n);
  });

  // Mode segmented
  function setModeButtons(m){
    document.querySelectorAll('#modeSeg button').forEach(b=>{
      b.classList.toggle('active', Number(b.dataset.m)===m);
    });
  }
  document.querySelectorAll('#modeSeg button').forEach(btn=>{
    btn.addEventListener('click', ()=>{
      const m = Number(btn.dataset.m)||0;
      setModeButtons(m);
      fetch('/api/mode?m='+m).catch(()=>{});
    });
  });

  // Play/Pause (corrected colors)
  $('btnPause').onclick = ()=>{
    const btn = $('btnPause');
    // If currently showing "Play" (paused), clicking will run -> show "Pause" (red)
    const showingPlay = btn.classList.contains('go');
    btn.disabled = true;
    if (showingPlay){
      btn.classList.remove('go'); btn.classList.add('stop'); btn.textContent='Pause';
    }else{
      btn.classList.remove('stop'); btn.classList.add('go'); btn.textContent='Play';
    }
    ignoreSseUntil = performance.now() + 400;
    fetch('/api/toggle').finally(()=> setTimeout(()=>btn.disabled=false, 250));
  };

  // Keyboard
  addEventListener('keydown', (e)=>{
    if (e.code==='Space'){ e.preventDefault(); $('btnPause').click(); return; }
    if (e.key==='1'){ fetch('/api/mode?m=0'); return; }
    if (e.key==='2'){ fetch('/api/mode?m=1'); return; }
    if (e.key==='3'){ fetch('/api/mode?m=2'); return; }
    if (e.key==='ArrowLeft'){ powerApply(uiPowerSet-1); return; }
    if (e.key==='ArrowRight'){ powerApply(uiPowerSet+1); return; }
    if (e.key==='ArrowUp'){ bpmApply(uiBpm+1); return; }
    if (e.key==='ArrowDown'){ bpmApply(uiBpm-1); return; }
  });
})();

/* ---------- SSE hookup ---------- */
const es = new EventSource('/stream');
let lastFrame = performance.now(), emaFPS = 0;

es.onopen  = ()=>{ $('sse').textContent='OPEN'; };
es.onerror = ()=>{ $('sse').textContent='ERROR'; };

function valveToSigned(v, fallbackMode){
  // Accept −1/0/+1 directly; map 0/1 to −1/+1; if bool → −1/+1. If unknown, use mode (1=REV→−1)
  if (typeof v === 'number'){
    if (v === -1 || v === 0 || v === 1) return v===0?0:v;
    if (v === 0) return -1;
    if (v > 0) return 1;
    if (v < 0) return -1;
  }
  if (v === true) return 1;
  if (v === false) return -1;
  if (fallbackMode === 1) return -1; // Reverse mode
  return 1; // default forward
}

es.onmessage = (ev)=>{
  // Stable FPS (EMA)
  const now = performance.now(), dt = now - lastFrame; lastFrame = now;
  const fps = 1000/Math.max(1,dt);
  emaFPS = emaFPS ? (emaFPS*0.98 + fps*0.02) : fps;
  $('fps').textContent = Math.round(emaFPS).toString();

  try{
    const d = JSON.parse(ev.data);

    // Push strips with FIXED Y ranges
    stripAtr.push( Number(d.atr_mmHg)   || 0 );
    stripVent.push(Number(d.vent_mmHg)  || 0 );
    stripFlow.push(Number(d.flow_ml_min)|| 0 );

    const signedValve = valveToSigned(d.valve, Number(d.mode));
    stripValve.push(signedValve);

    // Raw PWM to strip (clamped to [PWM_FLOOR,255], paused 0 will pin at bottom)
    let pwmRaw = (typeof d.pwm === 'number') ? d.pwm : null;
    if (pwmRaw==null && typeof d.powerPct === 'number'){
      // simple linear estimate if pwm not provided: floor..255
      pwmRaw = lastPwm;
    }
    if (pwmRaw==null) pwmRaw = lastPwm;
    stripPower.push(pwmRaw);
    lastPwm = pwmRaw;

    // Middle numerics
    setNum('n-atr',  d.atr_mmHg,1);
    setNum('n-vent', d.vent_mmHg,1);
    setNum('n-flow', d.flow_ml_min,1);

    // Valve arrow (green)
    const arrow = (signedValve>0)? '▲' : (signedValve<0)? '-' : '▼';
    $('n-valve-arrow').textContent = arrow;

    // Pump numeric shows SET POWER % (10..100)
    $('n-power').textContent = String(pwmRaw);

    // Mirror setpoints unless editing
    if (!editingPow && typeof d.powerPct === 'number'){
      uiPowerSet = Math.round(d.powerPct);
      $('powerLabel').textContent = uiPowerSet + '%';
    }
    if (!editingBpm && typeof d.bpm === 'number'){
      let b = Math.round(Number(d.bpm)||0);
      b = clamp(b,1,60);
      uiBpm = b; $('bpmLabel').textContent = String(b);
    }

    // BP
    computeBP();

    // Status (Hz from loopMs)
    const loopMs = Number(d.loopMs)||0;
    if (loopMs>0){
      $('hz').textContent = (1000/loopMs).toFixed(1);
      $('loop').textContent = loopMs.toFixed(2)+' ms';
    }

    // Mode & pause (avoid flicker after local click)
    const mode = Number(d.mode)||0;
    document.querySelectorAll('#modeSeg button').forEach(b=>{
      b.classList.toggle('active', Number(b.dataset.m)===mode);
    });

    if (performance.now() > ignoreSseUntil){
      const paused = !!d.paused;
      const btn = $('btnPause');
      if (paused){
        btn.classList.remove('stop'); btn.classList.add('go'); btn.textContent='Play';  // green
      }else{
        btn.classList.remove('go'); btn.classList.add('stop'); btn.textContent='Pause'; // red
      }
    }
  }catch(e){}
};

// First paints
addEventListener('load', ()=>{
  fitLayout();
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
