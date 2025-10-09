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
  html,body{height:100%}
  /* Kill page scroll & accidental horiz overflow */
  html,body{overflow:hidden}
  body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,Segoe UI,Roboto,Arial}

  /* Top status bar (compact) */
  .top{display:flex;gap:8px;align-items:center;padding:6px 10px;background:#0c1014;border-bottom:1px solid var(--grid)}
  .pill{font-size:12px;color:var(--muted);border:1px solid var(--grid);border-radius:999px;padding:2px 8px;white-space:nowrap}
  .pill b{color:var(--ink)}

  /* Main grid fills viewport height (set by JS), never wider than viewport */
  .main{
    display:grid;gap:12px;padding:10px;
    grid-template-columns:minmax(320px,1fr) minmax(96px,120px) minmax(240px,280px);
    max-width:100vw;  /* prevent rogue horizontal growth */
    height:calc(100vh - 40px); /* fallback; JS will refine */
  }

  /* Left: 5 equal rows */
  .leftgrid{
    display:grid;grid-template-rows:repeat(5,1fr);gap:var(--gap);min-height:0;
  }
  .strip{
    display:grid;grid-template-rows:auto 1fr auto;gap:0;
    background:var(--panel);border:1px solid var(--grid);border-radius:12px;overflow:hidden;min-height:0;
  }
  .stripHead{display:flex;justify-content:space-between;align-items:center;
    padding:4px 8px;color:var(--muted);font-size:11px;border-bottom:1px solid var(--grid);background:#0c1116}
  .canvasWrap{min-height:0}
  canvas{display:block;width:100%;height:100%;background:#0b0f13}
  .legend{display:flex;gap:10px;align-items:center;font-size:11px;color:var(--muted);
    padding:4px 8px;border-top:1px solid var(--grid);background:#0c1116}
  .sw{width:14px;height:3px;border-radius:2px;background:var(--muted)}

  /* Middle: 5 equal rows; tile height matches canvas row height */
  .midgrid{
    display:grid;grid-template-rows:repeat(5,1fr);gap:var(--gap);min-height:0;
  }
  .bigNum{
    display:flex;align-items:center;justify-content:center;height:100%;
    background:#0b0f13;border:1px solid var(--grid);border-radius:10px;min-height:0
  }
  .numStack{display:flex;flex-direction:column;align-items:center;line-height:1}
  .numStack .val{font-weight:900;letter-spacing:.02em;font-size:clamp(18px,3.4vh,28px)}
  .numStack .unit{font-size:11px;color:var(--muted);margin-top:4px}
  .t-cyan .val{color:var(--cyan)}
  .t-red .val{color:var(--red)}
  .t-amber .val{color:var(--amber)}
  .t-green .val{color:var(--green)}
  .t-violet .val{color:var(--violet)}

  /* Right: controls column — compact & scrollable if truly needed */
  .controls{display:flex;flex-direction:column;gap:8px;min-height:0;overflow:auto}
  .group{border:1px solid var(--grid);border-radius:10px;padding:8px;background:#0b0f13}
  .controls h3{margin:0 0 6px 0;font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}

  .bp{display:flex;align-items:baseline;gap:8px}
  .bp .main{font-weight:900;font-size:38px;letter-spacing:.02em}
  .bp .unit{color:var(--muted);font-size:12px}

  /* Vertical steppers */
  .stepper{display:grid;grid-template-rows:auto 1fr auto;align-items:center;justify-items:center;gap:6px;min-height:0}
  .stepper .btn{width:100%;padding:8px;border:1px solid var(--grid);border-radius:8px;background:#0f1317;color:var(--ink);cursor:pointer}
  .stepper .btn:hover{background:#101823}
  .stepper .readout{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;width:100%;
    border:1px solid var(--grid);border-radius:10px;background:#0f1317;padding:8px;min-height:0}
  .stepper .val{font-weight:900;font-size:22px}
  .stepper .val[contenteditable="true"]{outline:none;border-bottom:1px dashed var(--grid);padding:2px 6px;border-radius:6px}
  .stepper .sub{font-size:11px;color:var(--muted);margin-top:2px}

  .seg{display:flex;border:1px solid var(--grid);border-radius:10px;overflow:hidden}
  .seg button{flex:1;border:0;padding:8px 10px;background:#0f1317;color:var(--muted);cursor:pointer}
  .seg button.active{background:#142033;color:var(--ink)}
  .seg button:not(:last-child){border-right:1px solid var(--grid)}

  /* Brighter Play/Pause */
  .play{width:100%;padding:12px 10px;border-radius:10px;font-weight:800;border:1px solid var(--grid);cursor:pointer;transition:filter .12s ease}
  .play.run{background:#1f8f44;border-color:#2aa456;box-shadow:0 0 0 1px rgba(42,164,86,.25) inset}
  .play.pause{background:#a93636;border-color:#be4a4a;box-shadow:0 0 0 1px rgba(190,74,74,.25) inset}
  .play:hover{filter:brightness(1.06)}
  .play:disabled{opacity:0.6;cursor:not-allowed}

  .hint{font-size:11px;color:var(--muted)}

  /* Responsive: stack columns on narrow screens */
  @media (max-width: 900px){
    .main{grid-template-columns:1fr}
  }

  /* Short displays: compress legends to save vertical space */
  @media (max-height: 760px){
    .legend{padding:2px 8px;font-size:10px}
    .stripHead{padding:3px 8px}
    .bp .main{font-size:34px}
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

  <!-- Main layout (height locked to viewport by JS) -->
  <div class="main" id="main">
    <!-- LEFT: 5 strip charts -->
    <div class="leftgrid">
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
        <div class="legend"><span class="sw" style="background:#f59e0b"></span> −500 … 500 mL/min</div>
      </div>
      <div class="strip">
        <div class="stripHead"><span>Valve Direction</span><span>0–100</span></div>
        <div class="canvasWrap"><canvas id="cv-valve"></canvas></div>
        <div class="legend"><span class="sw" style="background:#22c55e"></span> 0 (REV) … 100 (FWD)</div>
      </div>
      <div class="strip">
        <div class="stripHead"><span>Pump (PWM scaled)</span><span>%</span></div>
        <div class="canvasWrap"><canvas id="cv-power"></canvas></div>
        <div class="legend"><span class="sw" style="background:#a78bfa"></span> 0 … 100 %</div>
      </div>
    </div>

    <!-- MIDDLE: 5 numeric tiles -->
    <div class="midgrid">
      <div class="bigNum t-cyan"><div class="numStack"><span class="val" id="n-atr">—</span><span class="unit">mmHg</span></div></div>
      <div class="bigNum t-red"><div class="numStack"><span class="val" id="n-vent">—</span><span class="unit">mmHg</span></div></div>
      <div class="bigNum t-amber"><div class="numStack"><span class="val" id="n-flow">—</span><span class="unit">mL/min</span></div></div>
      <div class="bigNum t-green"><div class="numStack"><span class="val" id="n-valve">—</span><span class="unit">/100</span></div></div>
      <div class="bigNum t-violet"><div class="numStack"><span class="val" id="n-power">—</span><span class="unit">%</span></div></div>
    </div>

    <!-- RIGHT: controls -->
    <div class="controls">
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
        <div class="stepper" id="stpBpm">
          <button class="btn" data-dir="up">▲</button>
          <div class="readout">
            <div class="val" id="bpmLabel" contenteditable="true" spellcheck="false">—</div>
            <div class="sub">BPM (1–60)</div>
          </div>
          <button class="btn" data-dir="down">▼</button>
        </div>
        <div class="hint">Click the number to type. Enter or blur to apply.</div>
      </div>

      <div class="group">
        <h3>Pump Power</h3>
        <div class="stepper" id="stpPower">
          <button class="btn" data-dir="up">▲</button>
          <div class="readout">
            <div class="val" id="powerLabel" contenteditable="true" spellcheck="false">—%</div>
            <div class="sub">Setpoint 10–100%</div>
          </div>
          <button class="btn" data-dir="down">▼</button>
        </div>
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
        <div class="hint">Space: Play/Pause • 1/2/3: Mode • ←/→: Power −/+1 • ↑/↓: BPM −/+1</div>
      </div>
    </div>
  </div>

<script>
/* ------------- Layout: lock grid to viewport height ------------- */
const $ = (id)=>document.getElementById(id);
$('ip').textContent = location.host || '192.168.4.1';

function fitLayout(){
  const topH = document.getElementById('topbar').getBoundingClientRect().height || 0;
  const main = document.getElementById('main');
  main.style.height = Math.max(300, window.innerHeight - topH - 10) + 'px';
}
addEventListener('resize', fitLayout);
addEventListener('load', fitLayout);

/* -------------------- Canvas helpers -------------------- */
function fitCanvas(canvas, ctx){
  const dpr = window.devicePixelRatio || 1;
  const cssW = Math.max(1, canvas.clientWidth);
  const cssH = Math.max(1, canvas.clientHeight);
  const W = Math.floor(cssW * dpr), H = Math.floor(cssH * dpr);
  if (canvas.width !== W || canvas.height !== H){
    canvas.width = W; canvas.height = H;
    ctx.setTransform(dpr,0,0,dpr,0,0);
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
const WINDOW_SEC = 5.0;

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

    ctx.lineWidth = 2; ctx.strokeStyle = color;
    for (let i=1;i<buf.length;i++){
      const a = buf[i-1], b = buf[i];
      const xa = X(a.t), xb = X(b.t);
      if (xb < xa && (xa - xb) > (W*0.2)) continue; // skip wrap jump
      let alpha = 1 - ((tNow - b.t) / WINDOW_SEC);
      if (alpha <= 0) continue;
      alpha *= alpha; // quadratic fade
      ctx.globalAlpha = alpha;
      ctx.beginPath(); ctx.moveTo(xa, Y(a.v)); ctx.lineTo(xb, Y(b.v)); ctx.stroke();
    }
    ctx.globalAlpha = 1;
  }
  addEventListener('resize', render);
  return {push, render, buf};
}

/* -------------------- Build strips -------------------- */
const stripAtr   = makeStrip('cv-atr',   {min:-5,   max:205,  color:'#0ea5e9'});
const stripVent  = makeStrip('cv-vent',  {min:-5,   max:205,  color:'#ef4444'});
const stripFlow  = makeStrip('cv-flow',  {min:-500, max:500,  color:'#f59e0b'});
const stripValve = makeStrip('cv-valve', {min:0,    max:100,  color:'#22c55e'});
const stripPower = makeStrip('cv-power', {min:0,    max:100,  color:'#a78bfa'}); // ACTUAL PWM%

/* -------------------- Live numerics + BP -------------------- */
function setNum(id, v, fixed=1){ $(id).textContent = (v==null||isNaN(v))?'—':Number(v).toFixed(fixed); }
function setText(id, s){ $(id).textContent = s; }

function computeBP(){
  const b = stripVent.buf;
  if (!b.length){ setText('bp','—/—'); return; }
  let hi = -1e9, lo = 1e9;
  for (const p of b){ if (p.v>hi) hi=p.v; if (p.v<lo) lo=p.v; }
  setText('bp', `${Math.round(hi)}/${Math.round(lo)}`);
}

/* -------------------- Controls -------------------- */
let uiBpm = 10, uiPowerSet = 30;
let editingBpm = false, editingPow = false;
let ignoreSseUntil = 0;

function clamp(v, lo, hi){ return Math.min(hi, Math.max(lo, v)); }
function parseIntStrict(s){
  const n = parseInt(String(s).trim().replace(/[^\d\-]/g,''),10);
  return isNaN(n)? null : n;
}

function bpmApply(newVal){
  const v = clamp(newVal|0, 1, 60);
  uiBpm = v; $('bpmLabel').textContent = String(v);
  fetch('/api/bpm?b='+v).catch(()=>{});
}
function powerApply(newVal){
  const v = clamp(newVal|0, 10, 100);
  uiPowerSet = v; $('powerLabel').textContent = v + '%';
  fetch('/api/power?pct='+v).catch(()=>{});
}
function stepHold(buttonEl, fn){
  let rpt=null;
  const start=()=>{ fn(); rpt=setInterval(fn, 140); };
  const stop =()=>{ if(rpt){clearInterval(rpt); rpt=null;} };
  buttonEl.addEventListener('mousedown', start);
  buttonEl.addEventListener('touchstart', (e)=>{ e.preventDefault(); start(); }, {passive:false});
  ['mouseup','mouseleave','touchend','touchcancel'].forEach(ev=> buttonEl.addEventListener(ev, stop));
}
(function bindSteppers(){
  const bpmUp = document.querySelector('#stpBpm [data-dir="up"]');
  const bpmDn = document.querySelector('#stpBpm [data-dir="down"]');
  const pUp   = document.querySelector('#stpPower [data-dir="up"]');
  const pDn   = document.querySelector('#stpPower [data-dir="down"]');

  stepHold(bpmUp, ()=> bpmApply(uiBpm + 1));
  stepHold(bpmDn, ()=> bpmApply(uiBpm - 1));
  stepHold(pUp,   ()=> powerApply(uiPowerSet + 1));
  stepHold(pDn,   ()=> powerApply(uiPowerSet - 1));

  const bpmField = $('bpmLabel');
  bpmField.addEventListener('focus', ()=> { editingBpm=true; });
  bpmField.addEventListener('keydown', (e)=>{ if(e.key==='Enter'){ e.preventDefault(); bpmField.blur(); }});
  bpmField.addEventListener('blur', ()=>{
    const n = parseIntStrict(bpmField.textContent);
    if (n==null){ bpmField.textContent = String(uiBpm); editingBpm=false; return; }
    editingBpm=false; bpmApply(n);
  });

  const pField = $('powerLabel');
  pField.addEventListener('focus', ()=> { editingPow=true; });
  pField.addEventListener('keydown', (e)=>{ if(e.key==='Enter'){ e.preventDefault(); pField.blur(); }});
  pField.addEventListener('blur', ()=>{
    const n = parseIntStrict(pField.textContent);
    if (n==null){ pField.textContent = uiPowerSet + '%'; editingPow=false; return; }
    editingPow=false; powerApply(n);
  });
})();

// Mode segmented buttons
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

// Play/Pause (brighter + anti-flicker)
$('btnPause').onclick = ()=>{
  const btn = $('btnPause');
  const willPause = !btn.classList.contains('pause');
  btn.disabled = true;
  btn.classList.toggle('run',  !willPause);
  btn.classList.toggle('pause', willPause);
  btn.textContent = willPause ? 'Play' : 'Pause';
  ignoreSseUntil = performance.now() + 400;
  fetch('/api/toggle').finally(()=> setTimeout(()=>btn.disabled=false, 250));
};

/* Keyboard shortcuts */
addEventListener('keydown', (e)=>{
  if (e.code==='Space'){ e.preventDefault(); $('btnPause').click(); return; }
  if (e.key==='1'){ fetch('/api/mode?m=0'); return; }
  if (e.key==='2'){ fetch('/api/mode?m=1'); return; }
  if (e.key==='3'){ fetch('/api/mode?m=2'); return; }
  if (e.key==='ArrowLeft'){ powerApply(uiPowerSet - 1); return; }
  if (e.key==='ArrowRight'){ powerApply(uiPowerSet + 1); return; }
  if (e.key==='ArrowUp'){ bpmApply(uiBpm + 1); return; }
  if (e.key==='ArrowDown'){ bpmApply(uiBpm - 1); return; }
});

/* -------------------- SSE hookup -------------------- */
const es = new EventSource('/stream');
let lastFrame = performance.now(), emaFPS = 0;

es.onopen  = ()=>{ $('sse').textContent='OPEN'; };
es.onerror = ()=>{ $('sse').textContent='ERROR'; };

es.onmessage = (ev)=>{
  // Stable FPS
  const now = performance.now(), dt = now - lastFrame; lastFrame = now;
  const fps = 1000/Math.max(1,dt);
  emaFPS = emaFPS ? (emaFPS*0.98 + fps*0.02) : fps;
  $('fps').textContent = Math.round(emaFPS).toString();

  try{
    const d = JSON.parse(ev.data);

    // Mirror setpoints (don’t overwrite while user is editing)
    if (!editingPow && typeof d.powerPct === 'number'){
      uiPowerSet = d.powerPct|0; $('powerLabel').textContent = uiPowerSet + '%';
    }
    if (!editingBpm && typeof d.bpm === 'number'){
      let b = Math.round(Number(d.bpm)||0);
      b = clamp(b,1,60);
      uiBpm = b; $('bpmLabel').textContent = String(b);
    }

    // Strip values
    const valvePct = d.valve ? 100 : 0;
    stripAtr.push( Number(d.atr_mmHg)   || 0 );
    stripVent.push(Number(d.vent_mmHg)  || 0 );
    stripFlow.push(Number(d.flow_ml_min)|| 0 );
    stripValve.push(valvePct);

    // Power strip uses ACTUAL PWM% (fallback to powerPct)
    let pwmPct = null;
    if (typeof d.pwm === 'number'){ pwmPct = clamp(Math.round((d.pwm/255)*100), 0, 100); }
    stripPower.push( (pwmPct!=null)? pwmPct : (Number(d.powerPct)||0) );

    // Middle numerics
    setNum('n-atr',   d.atr_mmHg,1);
    setNum('n-vent',  d.vent_mmHg,1);
    setNum('n-flow',  d.flow_ml_min,1);
    $('n-valve').textContent = valvePct.toFixed(0);
    $('n-power').textContent = (pwmPct!=null? pwmPct : (Number(d.powerPct)||0));

    // BP from ventricle buffer
    computeBP();

    // Status pills
    const loopMs = Number(d.loopMs)||0;
    if (loopMs>0){
      $('hz').textContent = (1000/loopMs).toFixed(1);
      $('loop').textContent = loopMs.toFixed(2)+' ms';
    }

    // Mode & pause (ignore brief flicker window after click)
    setModeButtons(Number(d.mode)||0);
    if (performance.now() > ignoreSseUntil){
      const paused = !!d.paused;
      const btn = $('btnPause');
      btn.classList.toggle('run',  !paused);
      btn.classList.toggle('pause', paused);
      btn.textContent = paused ? 'Play' : 'Pause';
    }
  }catch(e){}
};

// Initial paints
addEventListener('load', ()=>{
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
