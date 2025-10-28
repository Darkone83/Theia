#include "web_emu.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "wifimgr.h"      
#include "lcd_monitor.h"  

#ifndef WEB_EMU_KEEPALIVE_MS
#define WEB_EMU_KEEPALIVE_MS 15000UL
#endif

namespace WebEmu {

static AsyncEventSource sse("/emu/events");
static uint32_t last_sent_ms = 0;
static uint32_t last_ka_ms   = 0;

static String buildStateJson(const LCDMonitor::LCDState& st) {
  StaticJsonDocument<512> doc;
  doc["type"]  = "lcd20x4";
  doc["disp"]  = st.display_on;
  doc["cur"]   = st.cursor_on;
  doc["blink"] = st.blink_on;

  JsonObject cur = doc.createNestedObject("cursor");
  cur["r"] = st.cursor_row;
  cur["c"] = st.cursor_col;

  JsonArray rows = doc.createNestedArray("rows");
  for (int i = 0; i < 4; ++i) {
    char line[21]; memcpy(line, st.rows[i], 20); line[20] = '\0';
    rows.add(line);
  }
  String out; serializeJson(doc, out); return out;
}

void begin() {
  AsyncWebServer& server = WiFiMgr::getServer();

#if WEB_EMU_ENABLE_CORS
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
#endif

  server.on("/emu", HTTP_GET, [](AsyncWebServerRequest* req){
    static const char* kHtml = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Theia OLED Web Display</title>
<style>
  :root{
    --text:#e9e9e9;
    /* default (Blue) */
    --pixel:#d6e8ff; --glow:#8ac5ff;
    --backlight1:#122a3a; --backlight2:#153e5a;
    --grid:#0e1724;
    --contrast:1;
  }
  /* Skins */
  .skin-green { --pixel:#cfe8c6; --glow:#97ff8a; --backlight1:#163a16; --backlight2:#1f5a1f; --grid:#0e1a0e; }
  .skin-amber { --pixel:#ffefc7; --glow:#ffd68a; --backlight1:#3a2a12; --backlight2:#5a3b14; --grid:#241b0e; }
  .skin-ice   { --pixel:#e7fbff; --glow:#9fe7ff; --backlight1:#10303a; --backlight2:#115063; --grid:#0b2026; }
  .skin-violet{ --pixel:#f0e2ff; --glow:#d6a8ff; --backlight1:#27133b; --backlight2:#3a1f59; --grid:#1a0e29; }
  .skin-slate { --pixel:#e6f0f7; --glow:#a7c3de; --backlight1:#10151b; --backlight2:#1a2833; --grid:#0c1219; }

  html,body{height:100%}
  body{
    margin:0;background:#080808;color:var(--text);
    font:14px/1.4 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
    overflow-x:hidden;
  }

  header{padding:18px 16px 0 16px; text-align:center;}
  header h1{
    margin:0 auto; color:#fff; font-weight:700; font-size:20px; letter-spacing:.3px;
    font-family: ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  }

  .wrap{max-width:1100px;margin:10px auto 22px auto;padding:0 16px}
  .controls{
    display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin:8px 0 14px 0;
    position:sticky; top:8px; z-index:5; background:#080808; padding:6px 0;
  }
  .group{
    background:#111;border:1px solid #2a2a2a;border-radius:10px;padding:8px 10px;
    display:flex;align-items:center;gap:8px
  }
  label{font-size:.9em;color:#bbb;margin-right:4px}
  select,input[type=range]{
    background:#0b0b0b;border:1px solid #333;color:#ddd;border-radius:8px;padding:4px 6px
  }
  .pill{
    display:inline-flex;align-items:center;gap:6px;padding:.35em .6em;border-radius:999px;
    background:#111;border:1px solid #2a2a2a;font-size:.85em;white-space:nowrap
  }
  .pill strong{font-weight:700}
  .pills-below{
    display:flex; align-items:center; justify-content:center; flex-wrap:wrap; gap:8px;
    margin-top:10px;
  }
  .pills-below a{color:#9fd1ff; text-decoration:none}
  .pills-below a:hover{text-decoration:underline}

  .bezel{
    background:linear-gradient(180deg,#0d0d0d,#060606);
    border:1px solid #1b1b1b;border-radius:24px;padding:18px;
    box-shadow:0 10px 26px rgba(0,0,0,.55), inset 0 0 0 1px #000;
  }
  .glass{
    position:relative;border-radius:18px;padding:18px 16px;
    background:
      radial-gradient(120% 100% at 10% 0%, rgba(255,255,255,.08), rgba(255,255,255,0) 60%),
      linear-gradient(180deg,var(--backlight1),var(--backlight2));
    box-shadow: inset 0 2px 10px rgba(0,0,0,.6), inset 0 -2px 12px rgba(0,0,0,.5), 0 0 0 1px rgba(0,0,0,.75);
    filter:brightness(var(--contrast));
    overflow:hidden;
    /* no fixed min-height: JS sizes height to match exact LCD grid */
  }

  #lcd{
    display:block; width:100%; height:auto; image-rendering:crisp-edges; image-rendering:pixelated;
    /* height set by JS to match grid; width fills glass content box */
  }

  .hint{color:#999;font-size:.85em;margin-top:6px;text-align:center}
</style>
</head><body>
<header><h1>Theia OLED Web Display</h1></header>

<div class="wrap">
  <div class="controls">
    <!-- Skin + Pixel mode together -->
    <div class="group">
      <label>Skin</label>
      <select id="skin">
        <option value="blue">Blue</option>
        <option value="green">Green</option>
        <option value="amber">Amber</option>
        <option value="ice">Ice</option>
        <option value="violet">Violet</option>
        <option value="slate" selected>Slate</option>
      </select>
      <label style="margin-left:6px"><input id="pixelMode" type="checkbox"> Pixel mode</label>
    </div>

    <div class="group">
      <label>Contrast</label>
      <input id="contrast" type="range" min="60" max="130" value="100">
      <span id="contrastv" class="pill">100%</span>
    </div>
  </div>

  <div class="bezel">
    <div class="glass">
      <canvas id="lcd"></canvas>
    </div>
  </div>

  <!-- Centered pills BELOW the display -->
  <div class="pills-below">
    <div class="pill"><strong>disp</strong> <span id="mdisp">?</span></div>
    <div class="pill"><strong>cursor</strong> <span id="mcur">?</span></div>
    <div class="pill"><strong>blink</strong> <span id="mblink">?</span></div>
    <div class="pill"><strong>cursor@</strong> <span id="mpos">0,0</span></div>
    <div class="pill"><a href="/emu/state" target="_blank">/emu/state</a></div>
  </div>

  <div id="nodata" class="hint">⚠ No data detected — enable OLED support on your Xbox.</div>
</div>

<script>
  // ---------- DOM ----------
  const body=document.body;
  const glass=document.querySelector('.glass');
  const cvs=document.getElementById('lcd');
  const ctx=cvs.getContext('2d');

  const skinSel=document.getElementById('skin');
  const contrast=document.getElementById('contrast'); const contrastv=document.getElementById('contrastv');
  const pixelMode=document.getElementById('pixelMode');

  const mdisp=document.getElementById('mdisp'), mcur=document.getElementById('mcur'), mblink=document.getElementById('mblink'), mpos=document.getElementById('mpos');
  const msg=document.getElementById('nodata');

  // ---------- LCD logical geometry ----------
  const COLS=20, ROWS=4;
  const CELL_W=34, CELL_H=50;
  const GAP=8;
  const PAD_X=16, PAD_Y=16;

  const LOGICAL_W = PAD_X*2 + COLS*CELL_W + (COLS-1)*GAP;
  const LOGICAL_H = PAD_Y*2 + ROWS*CELL_H + (ROWS-1)*GAP;

  const DPR = Math.max(1, Math.min(2, window.devicePixelRatio || 1));

  // ---------- state ----------
  let haveData=false;
  let st = { disp:false, cur:false, blink:false, cursor:{r:0,c:0}, rows:["","","",""] };
  let pixelOn = false;

  // Helpers to get glass paddings
  function glassContentWidth(){
    const s = getComputedStyle(glass);
    const padL = parseFloat(s.paddingLeft) || 0;
    const padR = parseFloat(s.paddingRight) || 0;
    return Math.max(100, glass.clientWidth - padL - padR);
  }
  function glassContentPaddingTB(){
    const s = getComputedStyle(glass);
    const padT = parseFloat(s.paddingTop) || 0;
    const padB = parseFloat(s.paddingBottom) || 0;
    return { padT, padB };
  }

  // ---------- layout: scale to width, set glass/canvas height to the exact grid height ----------
  function layout() {
    const contentW = glassContentWidth();                 // inner width (excludes padding)
    const scale    = contentW / LOGICAL_W;                // scale from logical -> CSS px (width-locked)
    const drawnH   = LOGICAL_H * scale;                   // grid height at this scale

    // set glass overall height so top/bottom spacing is symmetrical (padding stays the same)
    const { padT, padB } = glassContentPaddingTB();
    glass.style.height = Math.round(drawnH + padT + padB) + 'px';

    // canvas CSS size = inner content box (width) x exact grid height
    cvs.style.width  = '100%';                            // fill content width
    cvs.style.height = Math.round(drawnH) + 'px';

    // canvas backing store (device pixels)
    cvs.width  = Math.floor(contentW * DPR);
    cvs.height = Math.floor(drawnH  * DPR);

    // set transform: map logical -> device px (width fit)
    ctx.setTransform(scale * DPR, 0, 0, scale * DPR, 0, 0);
  }

  window.addEventListener('resize', ()=>{ layout(); render(); });

  // ---------- skins / contrast ----------
  function setSkin(v){
    body.classList.remove('skin-green','skin-amber','skin-ice','skin-violet','skin-slate');
    if(v==='green')  body.classList.add('skin-green');
    if(v==='amber')  body.classList.add('skin-amber');
    if(v==='ice')    body.classList.add('skin-ice');
    if(v==='violet') body.classList.add('skin-violet');
    if(v==='slate')  body.classList.add('skin-slate');
    // default blue uses :root
  }
  function setContrast(pct){
    document.documentElement.style.setProperty('--contrast', (pct/100).toString());
    contrastv.textContent = pct + '%';
  }

  // ---------- drawing helpers ----------
  function roundRect(c, x, y, w, h, r){
    c.beginPath();
    c.moveTo(x+r, y);
    c.arcTo(x+w, y, x+w, y+h, r);
    c.arcTo(x+w, y+h, x, y+h, r);
    c.arcTo(x, y+h, x, y, r);
    c.arcTo(x, y, x+w, y, r);
    c.closePath();
  }

  let _probe;
  function getProbe(){
    if (!_probe){ _probe = document.createElement('canvas'); _probe.width=60; _probe.height=90; }
    return _probe;
  }

  function drawCell(x, y, ch){
    const r = 10;
    // panel
    const grdTop = ctx.createLinearGradient(0, y, 0, y+CELL_H);
    grdTop.addColorStop(0, 'rgba(0,0,0,0.25)');
    grdTop.addColorStop(1, 'rgba(0,0,0,0.35)');

    ctx.fillStyle = grdTop;
    roundRect(ctx, x, y, CELL_W, CELL_H, r); ctx.fill();
    ctx.lineWidth = 1; ctx.strokeStyle = '#0b0b0b'; ctx.stroke();

    // inner gloss
    const gloss = ctx.createLinearGradient(0, y, 0, y+CELL_H);
    gloss.addColorStop(0, 'rgba(255,255,255,0.06)');
    gloss.addColorStop(1, 'rgba(0,0,0,0.6)');
    ctx.strokeStyle = gloss; ctx.lineWidth = 2;
    roundRect(ctx, x+1, y+1, CELL_W-2, CELL_H-2, r-2); ctx.stroke();

    // glyph
    const px = getComputedStyle(body).getPropertyValue('--pixel').trim() || '#d6e8ff';
    const glow = getComputedStyle(body).getPropertyValue('--glow').trim() || '#8ac5ff';

    if (!pixelOn) {
      ctx.fillStyle = px;
      ctx.shadowColor = glow; ctx.shadowBlur = 12;
      ctx.font = 'bold 28px ui-monospace,Menlo,Consolas,monospace';
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(ch, x + CELL_W/2, y + CELL_H/2 + 2);
      ctx.shadowBlur = 0;
    } else {
      ctx.fillStyle = px;
      ctx.shadowColor = glow; ctx.shadowBlur = 10;
      const cols=5, rows=8, dotR=3;

      const p = getProbe();
      const pctx = p.getContext('2d');
      pctx.clearRect(0,0,p.width,p.height);
      pctx.fillStyle='#fff';
      pctx.font='bold 64px ui-monospace,Menlo,Consolas,monospace';
      pctx.textAlign='center'; pctx.textBaseline='middle';
      pctx.fillText(ch, p.width/2, p.height/2+4);
      const img = pctx.getImageData(0,0,p.width,p.height).data;
      const offX=8, offY=10, cellW=(p.width-16)/cols, cellH=(p.height-20)/rows;

      for(let ry=0; ry<rows; ry++){
        for(let cx=0; cx<cols; cx++){
          let acc=0, cnt=0;
          const sx=Math.floor(offX+cx*cellW), sy=Math.floor(offY+ry*cellH);
          const sw=Math.floor(cellW), sh=Math.floor(cellH);
          for(let yy=0; yy<sh; yy+=4){
            for(let xx=0; xx<sw; xx+=3){
              const pxi=4*((sy+yy)*p.width+(sx+xx));
              acc += img[pxi]+img[pxi+1]+img[pxi+2]; cnt++;
            }
          }
          if ((acc/(cnt*255*3)) > 0.22) {
            const dx = x + 6 + cx*(CELL_W-12)/(cols-1);
            const dy = y + 8 + ry*(CELL_H-16)/(rows-1);
            ctx.beginPath(); ctx.arc(dx,dy,dotR,0,Math.PI*2); ctx.fill();
          }
        }
      }
      ctx.shadowBlur=0;
    }
  }

  function render(){
    // clear full canvas
    ctx.clearRect(0, 0, cvs.width, cvs.height);

    // draw at (0,0) in logical coordinates (centering is handled by sizing)
    for (let r=0; r<ROWS; r++){
      const line = (st.rows[r]||'').padEnd(COLS,' ').slice(0,COLS);
      for (let c=0; c<COLS; c++){
        const x = PAD_X + c*(CELL_W+GAP);
        const y = PAD_Y + r*(CELL_H+GAP);
        drawCell(x, y, line[c]);
      }
    }
  }

  // ---------- controls ----------
  function applyState(newSt){
    if (!newSt) return;
    st = newSt;
    haveData = true; msg.style.display='none';
    mdisp.textContent = st.disp ? 'on' : 'off';
    mcur.textContent  = st.cur ? 'on' : 'off';
    mblink.textContent= st.blink ? 'on' : 'off';
    if (st.cursor && Number.isInteger(st.cursor.r) && Number.isInteger(st.cursor.c)){
      mpos.textContent = st.cursor.r + ',' + st.cursor.c;
    }
    render();
  }

  skinSel.addEventListener('change', e=>{ setSkin(e.target.value); render(); });
  contrast.addEventListener('input', e=> setContrast(+e.target.value));
  pixelMode.addEventListener('change', e=>{ pixelOn = e.target.checked; render(); });

  // ---------- init ----------
  setSkin('slate'); setContrast(100); pixelOn=false;
  layout(); render();

  // snapshot + SSE
  fetch('/emu/state',{cache:'no-store'}).then(r=>r.json()).then(j=>{
    if(j && j.rows && j.rows.length>0 && j.rows.some(line=>line.trim()!=="")) applyState(j);
  }).catch(()=>{});

  const es = new EventSource('/emu/events');
  es.addEventListener('message', e => { try { applyState(JSON.parse(e.data)); } catch(_){} });

  setTimeout(()=>{ if(!haveData) msg.style.display='block'; },10000);
</script>
</body></html>)HTML";

    auto* res = req->beginResponse(200, "text/html; charset=utf-8", kHtml);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  server.on("/emu/state", HTTP_GET, [](AsyncWebServerRequest* req){
    const auto& st = LCDMonitor::getDisplayState();
    String js = buildStateJson(st);
    auto* res = req->beginResponse(200, "application/json", js);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  sse.onConnect([](AsyncEventSourceClient* client){
    client->send("", "", millis(), 1500);
    const auto& st = LCDMonitor::getDisplayState();
    String js = buildStateJson(st);
    client->send(js.c_str(), "message");
  });

  server.addHandler(&sse);
}

void loop() {
  const auto& st = LCDMonitor::getDisplayState();

  if (st.last_update_ms != last_sent_ms) {
    last_sent_ms = st.last_update_ms;
    String js = buildStateJson(st);
    sse.send(js.c_str(), "message");
    last_ka_ms = millis();
    return;
  }
  const uint32_t now = millis();
  if (now - last_ka_ms > WEB_EMU_KEEPALIVE_MS) {
    sse.send("", "ka");
    last_ka_ms = now;
  }
}

} 
