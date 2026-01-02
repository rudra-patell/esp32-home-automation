#include <Arduino.h>

// src/main.cpp
// ESP32: STA-first, AP-fallback (exclusive) + SinricPro (3 cloud switches) + 4 relays
// IMPORTANT: HTTP server + WebSocket are ONLY started in AP mode (setup). When STA connects, servers are NOT running.

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <WebSocketsServer.h>

// -------- USER CONFIG --------
const char* FALLBACK_SSID = "Rudra 2.0";
const char* FALLBACK_PASS = "ssssssss";

const char* APP_KEY = "51641558-c57f-439e-b3d1-764ceb8ecfea";
const char* APP_SECRET = "628ffd4d-7e3d-41bd-9ed3-415101e6e160-777a36bd-3905-4589-b670-6515b06d9362";

const String DEVICE_ID_1 = "691815326dbd335b28df7c55";
const String DEVICE_ID_2 = "6918160000f870dd77b9a786";
const String DEVICE_ID_3 = "6918161c00f870dd77b9a7ab";

const int RELAY_PIN_1 = 16;
const int RELAY_PIN_2 = 17;
const int RELAY_PIN_3 = 18;
const int RELAY_PIN_4 = 19; // local-only

// *** NEW: IR proximity sensor analog pin & thresholds
const int IR_PIN = 34;                  // ADC pin for IR sensor (change if needed)
const int IR_ON_MIN = 2600;             // 2800 - 200
const int IR_ON_MAX = 3000;             // 2800 + 200

const bool RELAY_ACTIVE_LOW = true;

#define BOOT_BUTTON_PIN 0
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL; // 20s
const char* AP_PREFIX = "ESP32-Setup-";
const int LED_PIN = LED_BUILTIN; // GPIO2
// -------------------------------

// HTTP server (AP-mode setup)
WebServer server(80);
Preferences prefs;

// WebSocket server (only started in AP mode)
WebSocketsServer webSocket = WebSocketsServer(81);

// state
String apSSID;
bool serverRunning = false;   // true when AP server (HTTP) is running
bool wsRunning = false;       // true when websocket started (AP)
volatile int wsClients = 0;   // connected WS clients

bool relayState1 = false;
bool relayState2 = false;
bool relayState3 = false;
bool relayState4 = false;

// *** NEW: mode for local relay 4
enum Relay4Mode { RELAY4_MODE_OFF, RELAY4_MODE_ON, RELAY4_MODE_AUTO };
Relay4Mode relay4Mode = RELAY4_MODE_OFF;

// *** NEW: last IR raw value
int irRaw = 0;

// LED: simple status indicator (kept for local indication)
enum LedMode { LED_OFF, LED_SOLID, LED_FAST, LED_SLOW, LED_PATTERN };
LedMode ledMode = LED_SLOW;
unsigned long lastLedToggle = 0;
bool ledOnState = false;
int patternBlinkCount = 0;
int patternBlinkTarget = 0;
unsigned long patternLastChange = 0;
bool patternLedState = false;
const unsigned long PATTERN_ON_MS = 200;
const unsigned long PATTERN_OFF_MS = 200;

String generateAPSSID() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char tail[8];
  sprintf(tail, "%02X%02X", mac[4], mac[5]);
  return String(AP_PREFIX) + String(tail);
}

inline void writeRelay(int pin, bool on) {
  if (RELAY_ACTIVE_LOW) digitalWrite(pin, on ? LOW : HIGH);
  else digitalWrite(pin, on ? HIGH : LOW);
}

int deviceIdToPin(const String &deviceId) {
  if (deviceId == DEVICE_ID_1) return RELAY_PIN_1;
  if (deviceId == DEVICE_ID_2) return RELAY_PIN_2;
  if (deviceId == DEVICE_ID_3) return RELAY_PIN_3;
  return -1;
}
bool* deviceIdToState(const String &deviceId) {
  if (deviceId == DEVICE_ID_1) return &relayState1;
  if (deviceId == DEVICE_ID_2) return &relayState2;
  if (deviceId == DEVICE_ID_3) return &relayState3;
  return nullptr;
}

// *** NEW: helper to broadcast relay state array over WS
void broadcastRelayStates() {
  if (!wsRunning) return;
  String msg = String("{\"relay_states\":[") +
               (relayState1 ? "true" : "false") + "," +
               (relayState2 ? "true" : "false") + "," +
               (relayState3 ? "true" : "false") + "," +
               (relayState4 ? "true" : "false") + "]}";
  webSocket.broadcastTXT(msg);
}

// SinricPro callback
bool onPowerState(const String &deviceId, bool &state) {
  int pin = deviceIdToPin(deviceId);
  bool *pState = deviceIdToState(deviceId);
  if (pin < 0 || pState == nullptr) return false;
  writeRelay(pin, state);
  *pState = state;
  Serial.printf("[SinricPro] %s -> %s (pin %d)\n", deviceId.c_str(), state ? "ON":"OFF", pin);

  // If somehow WS running (shouldn't be in STA mode), broadcast state
  broadcastRelayStates();
  return true;
}

// LED helpers
void setLedMode(LedMode m) {
  ledMode = m;
  patternBlinkCount = 0; patternBlinkTarget = 0; patternLedState = false; patternLastChange = millis(); lastLedToggle = millis();
  if (m == LED_SOLID) { digitalWrite(LED_PIN, HIGH); ledOnState = true; }
  if (m == LED_OFF) { digitalWrite(LED_PIN, LOW); ledOnState = false; }
}

void updateLed() {
  unsigned long now = millis();
  if (ledMode == LED_PATTERN) {
    if (patternBlinkCount < patternBlinkTarget) {
      if (now - patternLastChange >= (patternLedState ? PATTERN_ON_MS : PATTERN_OFF_MS)) {
        patternLastChange = now;
        patternLedState = !patternLedState;
        digitalWrite(LED_PIN, patternLedState ? HIGH : LOW);
        if (!patternLedState) patternBlinkCount++;
      }
    } else {
      setLedMode(WiFi.status() == WL_CONNECTED ? LED_SOLID : LED_SLOW);
    }
    return;
  }
  if (ledMode == LED_SOLID) { digitalWrite(LED_PIN, HIGH); return; }
  if (ledMode == LED_OFF) { digitalWrite(LED_PIN, LOW); return; }
  unsigned long interval = (ledMode == LED_FAST) ? 120 : 800;
  if (now - lastLedToggle >= interval) {
    lastLedToggle = now;
    ledOnState = !ledOnState;
    digitalWrite(LED_PIN, ledOnState ? HIGH : LOW);
  }
}

// Try to connect STA only (no AP here)
bool attemptWiFiConnectSTA(const char* ssid, const char* pass, unsigned long timeout_ms) {
  Serial.printf("Attempting STA WiFi '%s' (timeout %lu ms)\n", ssid, timeout_ms);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  setLedMode(LED_FAST);
  while (millis() - start < timeout_ms) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("STA connected, IP: "); Serial.println(WiFi.localIP());
      setLedMode(LED_SOLID);
      return true;
    }
    updateLed();
    delay(30);
  }
  Serial.println("STA connect timed out");
  return false;
}

// Forward declarations
void setupRoutes();
void startSinricIfConnected();
void startAPModeAndServer(); // start AP + HTTP server + WS
void stopAPModeAndServer();  // stop AP + servers

// ------------------ WebSocket event handler (AP mode only) ------------------
void handleWsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    wsClients++;
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("WS Client %u connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
    // send current state immediately
    String msg = String("{\"relay_states\":[") +
                 (relayState1?"true":"false") + "," +
                 (relayState2?"true":"false") + "," +
                 (relayState3?"true":"false") + "," +
                 (relayState4?"true":"false") + "]}";
    webSocket.sendTXT(num, msg);
    return;
  }
  if (type == WStype_DISCONNECTED) {
    wsClients = max(0, wsClients - 1);
    Serial.printf("WS Client %u disconnected\n", num);
    return;
  }
  if (type == WStype_TEXT) {
    String s((char*)payload, length);
    Serial.printf("WS msg from %u: %s\n", num, s.c_str());
    if (s.startsWith("toggle:")) {
      int r = s.substring(7).toInt();
      if (r >=1 && r <=4) {
        int pin = (r==1?RELAY_PIN_1: r==2?RELAY_PIN_2: r==3?RELAY_PIN_3:RELAY_PIN_4);
        bool *ptr = (r==1?&relayState1: r==2?&relayState2: r==3?&relayState3:&relayState4);
        *ptr = !(*ptr);
        writeRelay(pin, *ptr);

        // *** NEW: if we manually toggle relay 4 via WS, force mode to ON/OFF
        if (r == 4) {
          relay4Mode = (*ptr ? RELAY4_MODE_ON : RELAY4_MODE_OFF);
        }

        if (WiFi.status() == WL_CONNECTED && r >=1 && r<=3) {
          SinricProSwitch &sw = SinricPro[(r==1?DEVICE_ID_1: r==2?DEVICE_ID_2: DEVICE_ID_3)];
          sw.sendPowerStateEvent(*ptr);
        }
        // broadcast to all WS clients
        broadcastRelayStates();
      }
    } else if (s == "status") {
      String msg = String("{\"relay_states\":[") +
                   (relayState1?"true":"false") + "," +
                   (relayState2?"true":"false") + "," +
                   (relayState3?"true":"false") + "," +
                   (relayState4?"true":"false") + "]}";
      webSocket.sendTXT(num, msg);
    }
  }
}

// ------------------ HTTP endpoints (AP mode only) ------------------
void setupRoutes() {
  server.on("/", HTTP_GET, [](){
    String html = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Setup</title>
<style>
body{background:#071018;color:#e6eef6;font-family:system-ui;padding:12px}
.btn{padding:8px 12px;margin:6px;border-radius:8px;background:#1f2937;border:none;color:#e6eef6;cursor:pointer}
.card{border-radius:10px;background:#0b1522;padding:10px;margin-top:10px}
</style>
</head><body>
<h2>ESP32 Setup (AP)</h2>
<div id="info">Connecting websocket...</div>

<div class="card">
  <h3>Relays</h3>
  <div id="relays"></div>
</div>

<div class="card">
  <h3>IR Proximity / Local Relay 4</h3>
  <div id="ir"></div>
</div>

<div class="card" id="networks">
  <h3>WiFi Setup</h3>
  <button class="btn" onclick="scan()">Scan</button>
  <div id="ss"></div>
</div>

<script>
let ws;

function buildRelays(states){
  const container = document.getElementById('relays');
  container.innerHTML = '';
  for(let i=0;i<4;i++){
    const b = document.createElement('button');
    b.className='btn';
    b.innerText = 'Relay ' + (i+1) + ': ' + (states[i] ? 'ON' : 'OFF');
    b.onclick = ()=> {
      if(ws && ws.readyState===1) ws.send('toggle:' + (i+1));
      else fetch('/toggle?relay=' + (i+1)).then(()=>refresh());
    };
    container.appendChild(b);
  }
}

// *** NEW: update IR info + mode controls
function updateIr(j){
  const d = document.getElementById('ir');
  const val = typeof j.ir_value !== 'undefined' ? j.ir_value : '-';
  const mode = j.relay4_mode || 'off';
  d.innerHTML =
    'IR value: <b>' + val + '</b><br>' +
    'Relay 4 mode: <b>' + mode.toUpperCase() + '</b><br>' +
    '<button class="btn" onclick="setR4Mode(\\'off\\')">Off</button>' +
    '<button class="btn" onclick="setR4Mode(\\'on\\')">On</button>' +
    '<button class="btn" onclick="setR4Mode(\\'auto\\')">Auto</button>';
}

function setR4Mode(m){
  fetch('/relay4_mode?mode=' + m).then(()=>refresh());
}

function refresh(){
  fetch('/status').then(r=>r.json()).then(j=>{
    buildRelays(j.relay_states);
    updateIr(j);
  });
}

function scan(){
  fetch('/scan').then(r=>r.json()).then(arr=>{
    const ss = document.getElementById('ss');
    ss.innerHTML='';
    arr.forEach(a=>{
      const d=document.createElement('div');
      d.innerHTML = '<b>'+a.ssid+'</b> ('+a.rssi+' dBm) <button class="btn" onclick="useS(\\''+a.ssid+'\\')">Use</button>';
      ss.appendChild(d);
    });
  });
}

function useS(ssid){
  const pass = prompt('Password for '+ssid);
  if(pass===null) return;
  const data = new URLSearchParams();
  data.append('ssid', ssid);
  data.append('pass', pass);
  fetch('/save', {method:'POST', body:data}).then(r=>r.text()).then(t=>alert(t));
}

function tryWebsocket(){
  const host = window.location.hostname;
  try {
    ws = new WebSocket('ws://' + host + ':81');
    ws.onopen = ()=> { document.getElementById('info').innerText='WebSocket connected'; ws.send('status'); };
    ws.onmessage = (evt)=> {
      try {
        const j = JSON.parse(evt.data);
        buildRelays(j.relay_states);
      } catch(e){ console.log('ws msg', evt.data); }
    };
    ws.onclose = ()=> { document.getElementById('info').innerText='WebSocket closed (falling back to HTTP)'; setTimeout(tryWebsocket,2000); };
    ws.onerror = ()=> { document.getElementById('info').innerText='WebSocket error'; setTimeout(tryWebsocket,2000); };
  } catch(e){
    document.getElementById('info').innerText='WebSocket not available';
  }
}

tryWebsocket();
setInterval(refresh, 5000);
</script>
</body></html>
)rawliteral";
    server.send(200, "text/html", html);
  });

  server.on("/scan", HTTP_GET, [](){
    int n = WiFi.scanNetworks();
    String out = "[";
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      bool sec = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      out += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(rssi) + ",\"secure\":" + (sec ? "true":"false") + "}";
      if (i < n-1) out += ",";
    }
    out += "]";
    if (n > 0) WiFi.scanDelete();
    server.send(200, "application/json", out);
  });

  server.on("/save", HTTP_POST, [](){
    if (!server.hasArg("ssid")) { server.send(400, "text/plain", "Missing ssid"); return; }
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    server.send(200, "text/plain", "Saved credentials — attempting to connect...");
    delay(200);

    // stop AP and servers
    if (wsRunning) {
      webSocket.disconnect(); // disconnect clients
      wsRunning = false;
    }
    if (serverRunning) {
      server.stop();
      serverRunning = false;
    }
    WiFi.softAPdisconnect(true);
    delay(200);

    // try STA connect
    bool ok = attemptWiFiConnectSTA(ssid.c_str(), pass.c_str(), 15000);
    if (ok) {
      startSinricIfConnected();
      // Do NOT start HTTP or WS in STA mode (explicitly stopped)
      Serial.println("Connected to STA after saving creds — SinricPro running, servers remain stopped.");
    } else {
      // fallback: restart AP+server so user can retry
      WiFi.mode(WIFI_AP);
      apSSID = generateAPSSID();
      WiFi.softAP(apSSID.c_str());
      delay(120);
      setupRoutes();
      server.begin();
      serverRunning = true;
      webSocket.begin();
      webSocket.onEvent(handleWsEvent);
      wsRunning = true;
      Serial.println("Connect failed — returned to AP mode with servers.");
    }
  });

  server.on("/toggle", HTTP_GET, [](){
    if (!server.hasArg("relay")) { server.send(400, "text/plain", "Missing relay"); return; }
    int r = server.arg("relay").toInt();
    if (r < 1 || r > 4) { server.send(400, "text/plain", "relay must be 1..4"); return; }
    int pin = (r==1?RELAY_PIN_1: r==2?RELAY_PIN_2: r==3?RELAY_PIN_3:RELAY_PIN_4);
    bool *ptr = (r==1?&relayState1: r==2?&relayState2: r==3?&relayState3:&relayState4);
    *ptr = !(*ptr);
    writeRelay(pin, *ptr);

    // *** NEW: if relay 4 toggled via HTTP, force mode
    if (r == 4) {
      relay4Mode = (*ptr ? RELAY4_MODE_ON : RELAY4_MODE_OFF);
    }

    if (WiFi.status() == WL_CONNECTED && r >=1 && r<=3) {
      SinricProSwitch &sw = SinricPro[(r==1?DEVICE_ID_1: r==2?DEVICE_ID_2: DEVICE_ID_3)];
      sw.sendPowerStateEvent(*ptr);
    }
    // broadcast over ws (if running)
    broadcastRelayStates();
    server.send(200, "text/plain", "OK");
  });

  // *** NEW: endpoint to set relay 4 mode (off/on/auto)
  server.on("/relay4_mode", HTTP_GET, [](){
    if (!server.hasArg("mode")) { server.send(400, "text/plain", "Missing mode"); return; }
    String m = server.arg("mode");
    Relay4Mode newMode;
    if (m == "off") newMode = RELAY4_MODE_OFF;
    else if (m == "on") newMode = RELAY4_MODE_ON;
    else if (m == "auto") newMode = RELAY4_MODE_AUTO;
    else { server.send(400, "text/plain", "mode must be off|on|auto"); return; }

    relay4Mode = newMode;

    if (relay4Mode == RELAY4_MODE_OFF) {
      relayState4 = false;
      writeRelay(RELAY_PIN_4, relayState4);
    } else if (relay4Mode == RELAY4_MODE_ON) {
      relayState4 = true;
      writeRelay(RELAY_PIN_4, relayState4);
    }
    // AUTO: relay will be updated in loop() based on IR sensor

    broadcastRelayStates();
    server.send(200, "text/plain", "OK");
  });

  server.on("/status", HTTP_GET, [](){
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    String staIp = wifiConnected ? WiFi.localIP().toString() : String("");
    String modeStr;
    if (WiFi.getMode() == WIFI_AP) modeStr = "AP";
    else if (wifiConnected) modeStr = "STA";
    else modeStr = "UNKNOWN";

    String relay4ModeStr = (relay4Mode == RELAY4_MODE_OFF) ? "off" :
                           (relay4Mode == RELAY4_MODE_ON)  ? "on"  : "auto";

    String out = "{";
    out += "\"mode\":\"" + modeStr + "\",";
    out += "\"wifi_connected\":" + String(wifiConnected ? "true":"false") + ",";
    out += "\"sta_ip\":\"" + staIp + "\",";
    out += "\"ap_ssid\":\"" + apSSID + "\",";
    out += "\"ir_value\":" + String(irRaw) + ",";
    out += "\"relay4_mode\":\"" + relay4ModeStr + "\",";
    out += "\"relay_states\":[" +
           String(relayState1?"true":"false") + "," +
           String(relayState2?"true":"false") + "," +
           String(relayState3?"true":"false") + "," +
           String(relayState4?"true":"false") + "]";
    out += "}";
    server.send(200, "application/json", out);
  });

  server.onNotFound([](){ server.send(404, "text/plain", "Not found"); });
}

// start SinricPro if STA connected
void startSinricIfConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    SinricProSwitch &sw1 = SinricPro[DEVICE_ID_1];
    SinricProSwitch &sw2 = SinricPro[DEVICE_ID_2];
    SinricProSwitch &sw3 = SinricPro[DEVICE_ID_3];
    sw1.onPowerState(onPowerState);
    sw2.onPowerState(onPowerState);
    sw3.onPowerState(onPowerState);
    SinricPro.begin(APP_KEY, APP_SECRET);
    Serial.println("SinricPro started");
  }
}

// Start AP + HTTP server + WS (used for forced-AP or fallback)
void startAPModeAndServer() {
  WiFi.mode(WIFI_AP);
  apSSID = generateAPSSID();
  WiFi.softAP(apSSID.c_str());
  delay(120);
  setupRoutes();
  server.begin();
  serverRunning = true;
  // start websocket for fast local control in AP
  webSocket.begin();
  webSocket.onEvent(handleWsEvent);
  wsRunning = true;
  Serial.printf("AP started (%s). HTTP & WS servers running.\n", apSSID.c_str());
}

// stop AP & servers (called before attempting STA)
void stopAPModeAndServer() {
  if (wsRunning) {
    webSocket.disconnect();
    wsRunning = false;
  }
  if (serverRunning) {
    server.stop();
    serverRunning = false;
  }
  WiFi.softAPdisconnect(true);
  delay(120);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);
  pinMode(RELAY_PIN_3, OUTPUT);
  pinMode(RELAY_PIN_4, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(IR_PIN, INPUT); // *** NEW: IR sensor pin

  // set relays off
  writeRelay(RELAY_PIN_1, false);
  writeRelay(RELAY_PIN_2, false);
  writeRelay(RELAY_PIN_3, false);
  writeRelay(RELAY_PIN_4, false);

  // load creds
  prefs.begin("wifi", true);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  // check BOOT button
  delay(50);
  bool bootPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  if (bootPressed) {
    Serial.println("BOOT pressed -> forced AP mode for setup");
    startAPModeAndServer();
  } else {
    // attempt STA
    bool connected = false;
    if (savedSsid.length()) connected = attemptWiFiConnectSTA(savedSsid.c_str(), savedPass.c_str(), WIFI_CONNECT_TIMEOUT_MS);
    if (!connected) connected = attemptWiFiConnectSTA(FALLBACK_SSID, FALLBACK_PASS, WIFI_CONNECT_TIMEOUT_MS);

    if (connected) {
      // STA mode: run SinricPro only (NO HTTP or WS)
      startSinricIfConnected();
      Serial.print("STA connected; SinricPro running. Servers NOT started. IP: ");
      Serial.println(WiFi.localIP());
      serverRunning = false;
      wsRunning = false;
    } else {
      // fallback to AP
      Serial.println("STA failed -> starting AP + servers for setup");
      startAPModeAndServer();
    }
  }

  setLedMode(WiFi.status()==WL_CONNECTED ? LED_SOLID : LED_SLOW);
}

void loop() {
  updateLed();

  // *** NEW: read IR sensor periodically and, if in AUTO, drive relay 4
  static unsigned long lastIrSample = 0;
  unsigned long now = millis();
  if (now - lastIrSample >= 100) {    // sample every 100 ms
    lastIrSample = now;
    irRaw = analogRead(IR_PIN);

    if (relay4Mode == RELAY4_MODE_AUTO) {
      bool targetOn = (irRaw >= IR_ON_MIN && irRaw <= IR_ON_MAX);
      if (targetOn != relayState4) {
        relayState4 = targetOn;
        writeRelay(RELAY_PIN_4, relayState4);
        broadcastRelayStates();
      }
    }
  }

  // AP mode: handle HTTP + WS (fast responsive)
  if (serverRunning) {
    server.handleClient();
  }
  if (wsRunning) {
    webSocket.loop();
  }

  // STA mode: only SinricPro cloud handling
  if (WiFi.status() == WL_CONNECTED) {
    SinricPro.handle();
  }

  // responsiveness: when AP clients connected keep tight loop, otherwise longer delay
  if (wsRunning && wsClients > 0) delay(10);
  else delay(150);
}