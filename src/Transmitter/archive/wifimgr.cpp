#include "wifimgr.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include "led_stat.h"
#include <vector>
#include "esp_wifi.h"
#include <Update.h> // For OTA
#include "lcd_monitor.h" // <-- for emulator enable/disable

static AsyncWebServer server(80);
namespace WiFiMgr {

static String ssid, password;
static Preferences prefs;
static DNSServer dnsServer;
static std::vector<String> lastScanResults;

enum class State { IDLE, CONNECTING, CONNECTED, PORTAL };
static State state = State::PORTAL;

static int connectAttempts = 0;
static const int maxAttempts = 10;
static unsigned long lastAttempt = 0;
static unsigned long retryDelay = 3000;

AsyncWebServer& getServer() {
    return server;
}

static void setAPConfig() {
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );
}

void loadCreds() {
    prefs.begin("wifi", true);
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    prefs.end();
}

void saveCreds(const String& s, const String& p) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    prefs.end();
}

void clearCreds() {
    prefs.begin("wifi", false);
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();
}

static String htmlSwitch(bool on) {
    // Simple CSS toggle switch (checkbox)
    String checked = on ? "checked" : "";
    return String() +
        "<label class='switch'>"
        "<input id='emuToggle' type='checkbox' " + checked + " onchange='toggleEmu(this.checked)'>"
        "<span class='slider round'></span>"
        "</label>";
}

void startPortal() {
    WiFi.disconnect(true);
    delay(100);
    setAPConfig();
    WiFi.mode(WIFI_AP_STA);  // AP+STA for S3
    delay(100);

    // Use channel 6 for iOS compatibility, or try 1
    bool apok = WiFi.softAP("Theia OLED EMU Setup", "", 6, 0);
    esp_wifi_set_max_tx_power(20);
    LedStat::setStatus(LedStatus::Portal);
    Serial.printf("[WiFiMgr] softAP result: %d, IP: %s\n", apok, WiFi.softAPIP().toString().c_str());
    delay(200);

    IPAddress apIP = WiFi.softAPIP();
    dnsServer.start(53, "*", apIP);

    server.reset(); // Needed to avoid double route definition on multiple starts

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        const bool emuOn = LCDMonitor::isEmulatorEnabled();
        String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>WiFi Setup</title>
    <meta name="viewport" content="width=320,initial-scale=1">
    <style>
        body {background:#111;color:#EEE;font-family:sans-serif;}
        .container {max-width:340px;margin:24px auto;background:#222;padding:2em;border-radius:12px;box-shadow:0 0 16px #0008;}
        input,select,button {width:100%;box-sizing:border-box;margin:.7em 0;padding:.6em;font-size:1.05em;border-radius:8px;border:1px solid #555;background:#111;color:#EEE;}
        .btn-primary {background:#299a2c;color:white;border-color:#299a2c;}
        .btn-danger {background:#a22;color:white;border-color:#a22;}
        .btn-ota {background:#265aa5;color:white;border-color:#265aa5;}
        .btn-web {background:#0d9488;color:white;border-color:#0d9488;}
        .status {margin-top:1em;font-size:.95em;}
        label {display:block;margin-top:.5em;margin-bottom:.1em;}
        .row {display:flex;gap:10px;align-items:center;}
        .row > * {flex:1;}
        .pill {display:inline-block;padding:.35em .6em;border-radius:999px;background:#333;border:1px solid #555;font-size:.85em;margin-left:6px;}
        /* Switch */
        .switch {position: relative; display: inline-block; width: 52px; height: 28px; vertical-align:middle;}
        .switch input {opacity: 0; width: 0; height: 0;}
        .slider {position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #555; transition: .2s; border-radius: 28px;}
        .slider:before {position: absolute; content: ""; height: 22px; width: 22px; left: 3px; bottom: 3px; background-color: white; transition: .2s; border-radius:50%;}
        input:checked + .slider {background-color: #299a2c;}
        input:checked + .slider:before {transform: translateX(24px);}
        .section {margin-top:18px;padding-top:12px;border-top:1px dashed #444;}
        .small {font-size:.9em;color:#bbb;}
    </style>
</head>
<body>
    <div class="container">
        <div style="width:100%;text-align:center;margin-bottom:1em">
            <span style="font-size:1.6em;font-weight:bold;">Theia OLED Emulator Setup</span>
        </div>

        <div class="section">
            <div class="row" style="justify-content:space-between">
                <div style="flex:unset"><b>LCD Emulator</b><span id="emuState" class="pill">...</span></div>
                <div style="flex:unset" id="emuSwitchHolder">__EMU_SWITCH__</div>
            </div>
            <div class="small">Toggle the US2066 emulator (I²C slave at 0x3C) on/off without reboot. Disabling releases the I²C bus.</div>
        </div>

        <div class="section">
            <form id="wifiForm" onsubmit="return false;">
                <label>WiFi Network</label>
                <select id="ssidDropdown" style="margin-bottom:1em;">
                    <option value="">Please select a network</option>
                </select>
                <input type="text" id="ssid" placeholder="SSID" style="margin-bottom:1em;">
                <label>Password</label>
                <input type="password" id="pass" placeholder="WiFi Password">
                <div class="row">
                    <button type="button" onclick="save()" class="btn-primary">Connect & Save</button>
                    <button type="button" onclick="forget()" class="btn-danger">Forget WiFi</button>
                </div>
                <button type="button" onclick="window.location='/ota'" class="btn-ota">OTA Update</button>
            </form>
            <div class="status" id="status">Status: ...</div>
        </div>

        <div class="section">
            <button type="button" onclick="window.location='/emu'" class="btn-web">Web View</button>
        </div>
    </div>
    <script>
        function setEmuStateTag(on){
            const tag = document.getElementById('emuState');
            tag.textContent = on ? 'ENABLED' : 'DISABLED';
            tag.style.background = on ? '#164b18' : '#4b1616';
            tag.style.borderColor = on ? '#299a2c' : '#a22';
        }
        function fetchEmuState(){
            fetch('/lcd/state').then(r=>r.json()).then(j=>{
                setEmuStateTag(!!j.enabled);
                const toggle = document.getElementById('emuToggle');
                if (toggle) toggle.checked = !!j.enabled;
            }).catch(()=>{});
        }
        function toggleEmu(on){
            fetch(on ? '/lcd/enable' : '/lcd/disable', {method:'POST'})
                .then(()=>fetchEmuState());
        }

        function uniq(arr){ return [...new Set(arr.filter(s=>s && s.trim().length))]; }
        let scanning=false;
        function scan() {
            if (scanning) return; scanning=true;
            fetch('/scan').then(r => r.json()).then(list => {
                list = uniq(list).sort((a,b)=>a.localeCompare(b));
                let dropdown = document.getElementById('ssidDropdown');
                dropdown.innerHTML = '';
                let defaultOpt = document.createElement('option');
                defaultOpt.value = '';
                defaultOpt.text = 'Please select a network';
                dropdown.appendChild(defaultOpt);
                list.forEach(ssid => {
                    let opt = document.createElement('option');
                    opt.value = ssid;
                    opt.text = ssid;
                    dropdown.appendChild(opt);
                });
                dropdown.onchange = function() {
                    document.getElementById('ssid').value = dropdown.value;
                };
            }).catch(() => {
                let dropdown = document.getElementById('ssidDropdown');
                dropdown.innerHTML = '';
                let opt = document.createElement('option');
                opt.value = '';
                opt.text = 'Scan failed';
                dropdown.appendChild(opt);
            }).finally(()=>{ scanning=false; });
        }
        setInterval(scan, 3000);
        window.onload = function(){
            scan();
            fetchEmuState();
        };
        function save() {
            let ssid = document.getElementById('ssid').value;
            let pass = document.getElementById('pass').value;
            fetch('/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ssid:ssid,pass:pass})
            }).then(r => r.text()).then(t => {
                document.getElementById('status').innerText = t;
            });
        }
        function forget() {
            fetch('/forget').then(r => r.text()).then(t => {
                document.getElementById('status').innerText = t;
                document.getElementById('ssid').value = '';
                document.getElementById('pass').value = '';
            });
        }
        // inject switch markup (kept inlined server-side for minimal templating)
        document.getElementById('emuSwitchHolder').innerHTML = `)rawliteral";
        page += htmlSwitch(emuOn);
        page += R"rawliteral(`.replace('__EMU_SWITCH__','');
    </script>
</body>
</html>
        )rawliteral";
        request->send(200, "text/html", page);
    });

    // === OTA PAGE ===
    server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request){
        String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>OTA Update</title>
    <meta name="viewport" content="width=320,initial-scale=1">
    <style>
        body {background:#111;color:#EEE;font-family:sans-serif;}
        .container {max-width:360px;margin:24px auto;background:#222;padding:2em;border-radius:12px;box-shadow:0 0 16px #0008;}
        input[type=file],button {width:100%;box-sizing:border-box;margin:.7em 0;padding:.6em;font-size:1.05em;border-radius:8px;border:1px solid #555;background:#111;color:#EEE;}
        .btn {background:#265aa5;color:white;border-color:#265aa5;}
        .status {margin-top:1em;font-size:.95em;}
        .barWrap {background:#111;border:1px solid #555;border-radius:8px;overflow:hidden;height:16px;margin-top:.5em}
        .bar {height:100%;width:0%;}
    </style>
</head>
<body>
    <div class="container">
        <h2>OTA Update</h2>
        <input id="fw" type="file" accept=".bin">
        <button class="btn" onclick="doUpload()">Upload & Flash</button>
        <div class="barWrap"><div id="bar" class="bar"></div></div>
        <div id="pct" class="status">0%</div>
        <div id="otaStatus" class="status"></div>
        <div class="row">
            <button class="btn" onclick="window.location='/'" style="margin-top:14px;">Back to WiFi Setup</button>
            <button class="btn" onclick="fetch('/reboot',{method:'POST'}).then(()=>{document.getElementById('otaStatus').innerText='Rebooting...';})" style="margin-top:14px;">Reboot Now</button>
        </div>
    </div>
    <script>
        function doUpload(){
            const f = document.getElementById('fw').files[0];
            if(!f){ alert('Choose a .bin first'); return; }
            const xhr = new XMLHttpRequest();
            xhr.open('POST','/update',true);
            xhr.upload.onprogress = (e)=>{
                if(e.lengthComputable){
                    const p = Math.round((e.loaded/e.total)*100);
                    document.getElementById('bar').style.width = p + '%';
                    document.getElementById('bar').style.background = p>=100 ? '#299a2c' : '#265aa5';
                    document.getElementById('pct').innerText = p + '%';
                }
            };
            xhr.onreadystatechange = ()=>{
                if(xhr.readyState===4){
                    if(xhr.status===200){
                        document.getElementById('otaStatus').innerText = 'Upload complete. Flash OK.';
                    } else {
                        document.getElementById('otaStatus').innerText = 'Upload finished with status ' + xhr.status;
                    }
                }
            };
            const form = new FormData();
            form.append('firmware', f, f.name);
            xhr.send(form);
        }
    </script>
</body>
</html>
        )rawliteral";
        request->send(200, "text/html", page);
    });

    // === OTA FIRMWARE UPLOAD HANDLER (chunked, progress on client). We assume success if upload completes ===
    server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest *request){
            // Will be responded in the upload handler's 'final' stage below
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            static bool updateError = false;
            if (!index) {
                Serial.printf("[OTA] Start update: %s (size unknown, streaming)\n", filename.c_str());
                updateError = false;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                    updateError = true;
                }
            }
            if (!updateError && !Update.hasError()) {
                size_t written = Update.write(data, len);
                if (written != len) {
                    Update.printError(Serial);
                    updateError = true;
                }
            }
            if (final) {
                bool ok = (!updateError && Update.end(true));
                if (!ok) {
                    Update.printError(Serial);
                    request->send(200, "text/plain", "Update processed, but reported an error: " + String(Update.errorString()));
                } else {
                    // Do NOT auto-restart here to avoid any browser hang mid-connection.
                    request->send(200, "text/plain", "OK");
                    Serial.println("[OTA] Update success (no auto-restart). Use /reboot or power-cycle.");
                }
            }
        }
    );

    // Optional explicit reboot endpoint to avoid rebooting in the upload callback
    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Rebooting...");
        Serial.println("[WiFiMgr] Reboot requested via /reboot");
        // small delay to allow response to flush
        delay(200);
        ESP.restart();
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
        String stat;
        if (WiFi.status() == WL_CONNECTED)
            stat = "Connected to " + WiFi.SSID() + " - IP: " + WiFi.localIP().toString();
        else if (state == State::CONNECTING)
            stat = "Connecting to " + ssid + "...";
        else
            stat = "In portal mode";
        request->send(200, "text/plain", stat);
    });

    server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request){
        String ss, pw;
        if (request->hasParam("ssid")) ss = request->getParam("ssid")->value();
        if (request->hasParam("pass")) pw = request->getParam("pass")->value();
        if (ss.length() == 0) {
            request->send(400, "text/plain", "SSID missing");
            return;
        }
        saveCreds(ss, pw);
        ssid = ss;
        password = pw;
        state = State::CONNECTING;
        connectAttempts = 1;
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        WiFi.begin(ssid.c_str(), password.c_str());
        request->send(200, "text/plain", "Connecting to: " + ssid);
    });

    // /scan endpoint: reliable + cached + unique SSIDs
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        int n = WiFi.scanComplete();
        if (n == -2) {
            // no scan started; kick async scan but return cache
            WiFi.scanNetworks(true, true);
        } else if (n >= 0) {
            lastScanResults.clear();
            lastScanResults.reserve(n);
            for (int i = 0; i < n; ++i) {
                String s = WiFi.SSID(i);
                if (s.length()) lastScanResults.push_back(s);
            }
            WiFi.scanDelete();
            // restart async scan for next time
            WiFi.scanNetworks(true, true);
        }
        // de-dup + sort
        std::sort(lastScanResults.begin(), lastScanResults.end());
        lastScanResults.erase(std::unique(lastScanResults.begin(), lastScanResults.end()), lastScanResults.end());
        // return as JSON array of strings (keeps UI unchanged)
        String json = "[";
        for (size_t i = 0; i < lastScanResults.size(); ++i) {
            if (i) json += ",";
            json += "\"" + lastScanResults[i] + "\"";
        }
        json += "]";
        request->send(200, "application/json", json);
    });

    server.on("/forget", HTTP_GET, [](AsyncWebServerRequest *request){
        clearCreds();
        ssid = ""; password = "";
        WiFi.disconnect();
        state = State::PORTAL;
        request->send(200, "text/plain", "WiFi credentials cleared.");
    });

    server.on("/debug/forget", HTTP_GET, [](AsyncWebServerRequest *request){
        clearCreds();
        ssid = "";
        password = "";
        WiFi.disconnect(true);
        state = State::PORTAL;
        Serial.println("[DEBUG] WiFi credentials cleared via /debug/forget");
        request->send(200, "text/plain", "WiFi credentials cleared (debug).");
    });

    // Proper POST JSON body for ESPAsyncWebServer (Arduino 3.2.0)
    server.on("/save", HTTP_POST,
        [](AsyncWebServerRequest *request){},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
            String body; body.reserve(len);
            for (size_t i = 0; i < len; i++) body += (char)data[i];
            // crude parse: {"ssid":"...","pass":"..."}
            int ssidStart = body.indexOf("\"ssid\":\"") + 8;
            int ssidEnd   = body.indexOf("\"", ssidStart);
            int passStart = body.indexOf("\"pass\":\"") + 8;
            int passEnd   = body.indexOf("\"", passStart);
            String newSsid = (ssidStart >= 8 && ssidEnd > ssidStart) ? body.substring(ssidStart, ssidEnd) : "";
            String newPass = (passStart >= 8 && passEnd > passStart) ? body.substring(passStart, passEnd) : "";
            if (newSsid.length() == 0) {
                request->send(400, "text/plain", "SSID missing");
                return;
            }
            saveCreds(newSsid, newPass);
            ssid = newSsid;
            password = newPass;
            state = State::CONNECTING;
            connectAttempts = 1;
            WiFi.begin(newSsid.c_str(), newPass.c_str());
            request->send(200, "text/plain", "Connecting to: " + newSsid);
            Serial.printf("[WiFiMgr] Received new creds. SSID: %s\n", newSsid.c_str());
        }
    );

    // ---------- LCD Emulator control endpoints ----------
    server.on("/lcd/state", HTTP_GET, [](AsyncWebServerRequest *request){
        bool en = LCDMonitor::isEmulatorEnabled();
        String j = String("{\"enabled\":") + (en ? "true" : "false") + "}";
        request->send(200, "application/json", j);
    });
    server.on("/lcd/enable", HTTP_ANY, [](AsyncWebServerRequest *request){
        LCDMonitor::setEmulatorEnabled(true);
        request->send(200, "text/plain", "LCD emulator enabled");
    });
    server.on("/lcd/disable", HTTP_ANY, [](AsyncWebServerRequest *request){
        LCDMonitor::setEmulatorEnabled(false);
        request->send(200, "text/plain", "LCD emulator disabled");
    });

    auto cp = [](AsyncWebServerRequest *r){
        r->send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/' />");
    };
    server.on("/generate_204", HTTP_GET, cp);
    server.on("/hotspot-detect.html", HTTP_GET, cp);
    server.on("/redirect", HTTP_GET, cp);
    server.on("/ncsi.txt", HTTP_GET, cp);
    server.on("/captiveportal", HTTP_GET, cp);
    server.onNotFound(cp);

    server.begin();
    state = State::PORTAL;
}

void stopPortal() {
    dnsServer.stop();
}

void tryConnect() {
    if (ssid.length() > 0) {
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        WiFi.begin(ssid.c_str(), password.c_str());
        state = State::CONNECTING;
        connectAttempts = 1;
        lastAttempt = millis();
    } else {
        startPortal();
    }
}

void begin() {
    LedStat::setStatus(LedStatus::Booting);
    loadCreds();
    startPortal();
    if (ssid.length() > 0)
        tryConnect();
}

void loop() {
    dnsServer.processNextRequest();
    if (state == State::CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            state = State::CONNECTED;
            dnsServer.stop();
            Serial.println("[WiFiMgr] WiFi connected.");
            Serial.print("[WiFiMgr] IP Address: ");
            Serial.println(WiFi.localIP());
            LedStat::setStatus(LedStatus::WifiConnected);
        } else if (millis() - lastAttempt > retryDelay) {
            connectAttempts++;
            if (connectAttempts >= maxAttempts) {
                state = State::PORTAL;
                startPortal();
                LedStat::setStatus(LedStatus::WifiFailed);
            } else {
                WiFi.disconnect();
                WiFi.begin(ssid.c_str(), password.c_str());
                lastAttempt = millis();
            }
        }
    }
}

void restartPortal() {
    startPortal();
}

void forgetWiFi() {
    clearCreds();
    startPortal();
}

void forgetWiFiFromSerial() {
    clearCreds();
    WiFi.disconnect(true);
    ssid = "";
    password = "";
    Serial.println("[SerialCmd] WiFi credentials forgotten.");
    startPortal();
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String getStatus() {
    if (isConnected()) return "Connected to: " + ssid;
    if (state == State::CONNECTING) return "Connecting to: " + ssid;
    return "Not connected";
}

} // namespace WiFiMgr
