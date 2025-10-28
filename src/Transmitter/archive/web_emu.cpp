#include "web_emu.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "wifimgr.h"     
#include "lcd_monitor.h"    

namespace WebEmu {

static AsyncEventSource sse("/emu/events");
static uint32_t last_sent_ms = 0;

// ---- helpers ----
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
    char line[21]; memcpy(line, st.rows[i], 21);
    line[20] = '\0';
    rows.add(line);
  }

  String out;
  serializeJson(doc, out);
  return out;
}

void begin() {
  AsyncWebServer& server = WiFiMgr::getServer();

  // ---- HTML UI ----
  server.on("/emu", HTTP_GET, [](AsyncWebServerRequest* req){
    static const char* kHtml = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Theia Web Emulator</title>
<style>
  :root{--bg:#0b0b0b;--panel:#151515;--text:#e9e9e9;--muted:#9aa0a6;--grid:#222;}
  body{margin:0;background:var(--bg);color:var(--text);font:14px/1.4 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;}
  .wrap{max-width:880px;margin:24px auto;padding:0 16px;}
  .card{background:var(--panel);border-radius:14px;box-shadow:0 8px 24px rgba(0,0,0,.35);padding:16px 16px 18px;}
  h1{margin:0 0 12px;font:600 18px system-ui,Segoe UI,Roboto,Helvetica,Arial;}
  .lcd{display:grid;grid-template-columns:repeat(20,1fr);gap:4px;background:var(--grid);padding:10px;border-radius:10px;}
  .row{display:grid;grid-template-columns:repeat(20,1fr);gap:4px;margin-bottom:4px}
  .cell{background:#0f172a;border-radius:6px;padding:6px 4px;min-width:10px;text-align:center;white-space:pre;color:#cbd5e1}
  .meta{display:flex;gap:16px;color:var(--muted);margin:10px 0 0;flex-wrap:wrap}
  .pill{background:#111;border:1px solid #2a2a2a;border-radius:999px;padding:2px 8px}
  .bar{height:4px;border-radius:999px;background:linear-gradient(90deg,#22c55e,#06b6d4);opacity:.6;margin:8px 0}
  .nodata{text-align:center;color:#bbb;font-size:1em;margin:24px 0;}
  a{color:#7dd3fc;text-decoration:none}
</style>
</head><body>
<div class="wrap">
  <div class="card">
    <h1>Theia Web Emulator</h1>
    <div id="lcd"></div>
    <div id="nodata" class="nodata">⚠ No data detected — enable OLED support on your Xbox.</div>
    <div class="bar"></div>
    <div class="meta">
      <div class="pill"><strong>disp</strong>: <span id="mdisp">?</span></div>
      <div class="pill"><strong>cursor</strong>: <span id="mcur">?</span></div>
      <div class="pill"><strong>blink</strong>: <span id="mblink">?</span></div>
      <div class="pill"><strong>cursor@</strong>: <span id="mpos">0,0</span></div>
      <div class="pill"><a href="/emu/state" target="_blank">/emu/state</a></div>
    </div>
  </div>
</div>
<script>
  const lcd=document.getElementById('lcd');
  const msg=document.getElementById('nodata');
  const mdisp=document.getElementById('mdisp');
  const mcur=document.getElementById('mcur');
  const mblink=document.getElementById('mblink');
  const mpos=document.getElementById('mpos');
  let haveData=false;

  function render(rows){
    lcd.innerHTML='';
    for(let r=0;r<4;r++){
      const row=document.createElement('div'); row.className='row';
      const txt=(rows[r]||'').padEnd(20,' ').slice(0,20);
      for(let c=0;c<20;c++){
        const d=document.createElement('div'); d.className='cell';
        d.textContent = txt[c];
        row.appendChild(d);
      }
      lcd.appendChild(row);
    }
  }

  function apply(state){
    if(!state) return;
    haveData=true;
    msg.style.display='none';
    render(state.rows||[]);
    mdisp.textContent = state.disp ? 'on' : 'off';
    mcur.textContent  = state.cur ? 'on' : 'off';
    mblink.textContent= state.blink ? 'on' : 'off';
    if(state.cursor && typeof state.cursor.r==='number' && typeof state.cursor.c==='number'){
      mpos.textContent = state.cursor.r + ',' + state.cursor.c;
    }
  }

  // Initial fetch
  fetch('/emu/state').then(r=>r.json()).then(j=>{
    if(j && j.rows && j.rows.length>0 && j.rows.some(line=>line.trim()!=="")) apply(j);
  }).catch(()=>{});

  // Live updates via SSE
  const es = new EventSource('/emu/events');
  es.addEventListener('message', e => {
    try {
      const st = JSON.parse(e.data);
      apply(st);
    } catch(_){}
  });

  // If no data after 10s, show message (already visible by default)
  setTimeout(()=>{
    if(!haveData) msg.style.display='block';
  },10000);
</script>
</body></html>)HTML";
    req->send(200, "text/html; charset=utf-8", kHtml);
  });

  // ---- JSON state ----
  server.on("/emu/state", HTTP_GET, [](AsyncWebServerRequest* req){
    const auto& st = LCDMonitor::getDisplayState();
    req->send(200, "application/json", buildStateJson(st));
  });

  // ---- SSE stream ----
  server.addHandler(&sse);
}

void loop() {
  const auto& st = LCDMonitor::getDisplayState();
  if (st.last_update_ms != last_sent_ms) {
    last_sent_ms = st.last_update_ms;
    const String json = buildStateJson(st);
    sse.send(json.c_str(), "message");
  }
}

} // namespace WebEmu
