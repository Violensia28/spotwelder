#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// --- DEKLARASI FUNGSI (PROTOTYPE) ---
void performWeld();
float read_AC_RMS_Current(); // <--- TAMBAHAN PERBAIKAN
float read_AC_RMS_Voltage(); // <--- TAMBAHAN PERBAIKAN

// --- OBJEK & KONFIGURASI ---
Preferences preferences;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char* ssid = "GeminiSpot_WIFI";
const char* password = "password123";

// --- PIN DEFAULT ---
#define DEFAULT_PIN_SSR 23
#define DEFAULT_PIN_FOOTSWITCH 34
#define DEFAULT_PIN_ACS 35
#define DEFAULT_PIN_ZMPT 32

// --- STRUKTUR DATA KONFIGURASI ---
struct Config {
    struct Guards {
        float v_cutoff = 180.0;
        float i_guard = 25.0;
        bool mcb_guard = true;
    } guards;
    struct AutoTrigger {
        bool enabled = false;
        float threshold = 3.5; // Threshold dalam Ampere
        int ping_interval = 100; // ms
    } autotrigger;
} config;

// --- VARIABEL RUNTIME ---
enum WeldMode { TIME_MODE, ENERGY_MODE, DUAL_PULSE_MODE } currentMode = TIME_MODE;
long timePulse = 20;
bool isWelding = false;
volatile bool webTriggerActive = false;
int adc_zero_point_current = 2048;
int adc_zero_point_voltage = 2048;
unsigned long lastPingTime = 0;

// --- HALAMAN WEB ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>GeminiSpot W-Series</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body{font-family:Arial,sans-serif;background-color:#121212;color:#e0e0e0;text-align:center;margin:0;padding:0}.container{max-width:400px;margin:0 auto;padding:10px}.panel{background-color:#1e1e1e;padding:15px;margin-bottom:15px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.5)}h1,h2{color:#bb86fc;margin-top:0}.status{font-size:1.5em;font-weight:700;color:#03dac6;margin:10px 0}button{background-color:#333;border:1px solid #bb86fc;color:#bb86fc;padding:10px;margin:5px;border-radius:5px;cursor:pointer}button.active{background-color:#bb86fc;color:#121212}input,label{color:#e0e0e0;background-color:#333;border:1px solid #999;border-radius:4px;padding:8px}.weld-btn{-webkit-user-select:none;user-select:none;background-color:#cf6679;color:white;padding:20px;font-size:1.5em;border:none;border-radius:50%;width:150px;height:150px;cursor:pointer;margin-top:20px}.weld-btn:active{background-color:#ff8a80}
    </style>
</head>
<body>
    <div class="container">
        <h1>GeminiSpot W-Series</h1>
        <div class="panel">
            <h2>STATUS</h2>
            <div id="status" class="status">Connecting...</div>
        </div>
        <div class="panel">
            <h2>AKSI</h2>
            <button id="weld_btn" class="weld-btn">TAHAN UNTUK LAS</button>
        </div>
        <div id="autotrigger_panel" class="panel">
            <h2>Auto-Trigger (ACS712)</h2>
            <p><label for="autotrigger_enabled">Aktifkan: </label><input type="checkbox" id="autotrigger_enabled"></p>
            <p><label for="autotrigger_threshold">Sensitivitas (A): </label><input type="number" id="autotrigger_threshold" step="0.1"></p>
            <button onclick="setConfig('autotrigger')">Simpan Auto-Trigger</button>
        </div>
        <div id="guards_panel" class="panel">
            <h2>Guards (Keamanan)</h2>
            <p><label for="v_cutoff_val">V-Cutoff (V): </label><input type="number" id="v_cutoff_val" step="1"></p>
            <p><label for="i_guard_val">I-Guard (A): </label><input type="number" id="i_guard_val" step="0.5"></p>
            <p><label for="mcb_guard_val">MCB Guard: </label><input type="checkbox" id="mcb_guard_val"></p>
            <button onclick="setConfig('guards')">Simpan Guards</button>
        </div>
    </div>
    <script>
        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        var audioCtx, oscillator;

        function initAudio(){try{audioCtx=new(window.AudioContext||window.webkitAudioContext)}catch(e){console.log('Web Audio API is not supported')}}
        function playBeep(){if(!audioCtx||oscillator)return;oscillator=audioCtx.createOscillator();oscillator.type='sine';oscillator.frequency.setValueAtTime(880,audioCtx.currentTime);oscillator.connect(audioCtx.destination);oscillator.start()}
        function stopBeep(){if(oscillator){oscillator.stop();oscillator.disconnect();oscillator=null}}
        function initWebSocket(){websocket=new WebSocket(gateway);websocket.onopen=onOpen;websocket.onclose=onClose;websocket.onmessage=onMessage}
        function onOpen(event){document.getElementById('status').innerText="Connected";websocket.send('{"action":"get_config"}')}
        function onClose(event){document.getElementById('status').innerText="Closed";setTimeout(initWebSocket,2000)}
        function onMessage(event){var data=JSON.parse(event.data);if(data.action==='config_update'){let guards=data.payload.guards;document.getElementById('v_cutoff_val').value=guards.v_cutoff;document.getElementById('i_guard_val').value=guards.i_guard;document.getElementById('mcb_guard_val').checked=guards.mcb_guard;let autotrigger=data.payload.autotrigger;document.getElementById('autotrigger_enabled').checked=autotrigger.enabled;document.getElementById('autotrigger_threshold').value=autotrigger.threshold;document.getElementById('status').innerText="Ready"}else if(data.action==='status_update'){document.getElementById('status').innerText=data.status}}
        function setConfig(section){let payload={};if(section==='guards'){payload.guards={v_cutoff:parseFloat(document.getElementById('v_cutoff_val').value),i_guard:parseFloat(document.getElementById('i_guard_val').value),mcb_guard:document.getElementById('mcb_guard_val').checked}}else if(section==='autotrigger'){payload.autotrigger={enabled:document.getElementById('autotrigger_enabled').checked,threshold:parseFloat(document.getElementById('autotrigger_threshold').value)}};let msg={action:"set_config",payload:payload};websocket.send(JSON.stringify(msg));document.getElementById('status').innerText="Menyimpan..."}
        function triggerWeld(start){websocket.send(`{"action":"weld_trigger", "value":${start}}`);if(start){playBeep()}else{stopBeep()}}
        
        window.addEventListener('load', function(){
            initWebSocket();
            initAudio();
            let weldBtn = document.getElementById('weld_btn');
            weldBtn.addEventListener('mousedown', () => triggerWeld(true));
            weldBtn.addEventListener('mouseup', () => triggerWeld(false));
            weldBtn.addEventListener('mouseleave', () => triggerWeld(false));
            weldBtn.addEventListener('touchstart', (e) => { e.preventDefault(); triggerWeld(true); });
            weldBtn.addEventListener('touchend', (e) => { e.preventDefault(); triggerWeld(false); });
        });
    </script>
</body>
</html>
)rawliteral";


// --- FUNGSI SETUP & LOOP ---

void setup() {
  Serial.begin(115200);
  loadConfig(); 
  
  pinMode(DEFAULT_PIN_SSR, OUTPUT);
  digitalWrite(DEFAULT_PIN_SSR, LOW);
  pinMode(DEFAULT_PIN_FOOTSWITCH, INPUT_PULLUP);
  
  long total_current = 0, total_voltage = 0;
  for (int i = 0; i < 500; i++) { 
    total_current += analogRead(DEFAULT_PIN_ACS); 
    total_voltage += analogRead(DEFAULT_PIN_ZMPT);
    delay(1); 
  }
  adc_zero_point_current = total_current / 500;
  adc_zero_point_voltage = total_voltage / 500;

  WiFi.softAP(ssid, password);
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.begin();
  Serial.println("GeminiSpot W-Series W3 (Fix) Initialized.");
}

void loop() {
  if (digitalRead(DEFAULT_PIN_FOOTSWITCH) == LOW && !isWelding) {
    performWeld();
  }
  if (webTriggerActive && !isWelding) {
    performWeld();
  }
  if (config.autotrigger.enabled && !isWelding && (millis() - lastPingTime > config.autotrigger.ping_interval)) {
    lastPingTime = millis();
    digitalWrite(DEFAULT_PIN_SSR, HIGH);
    delayMicroseconds(500);
    digitalWrite(DEFAULT_PIN_SSR, LOW);
    float ping_current = read_AC_RMS_Current();
    if (ping_current > config.autotrigger.threshold) {
        Serial.printf("Auto-Trigger fired! Current: %.2fA, Threshold: %.2fA\n", ping_current, config.autotrigger.threshold);
        performWeld();
    }
  }
  ws.cleanupClients();
}

// --- FUNGSI LOGIKA & PEMBANTU ---

void loadConfig() {
    preferences.begin("geminispot", false);
    config.guards.v_cutoff = preferences.getFloat("g_v_cutoff", 180.0);
    config.guards.i_guard = preferences.getFloat("g_i_guard", 25.0);
    config.guards.mcb_guard = preferences.getBool("g_mcb_guard", true);
    config.autotrigger.enabled = preferences.getBool("at_enabled", false);
    config.autotrigger.threshold = preferences.getFloat("at_thresh", 3.5);
    preferences.end();
}

void saveConfig() {
    preferences.begin("geminispot", false);
    preferences.putFloat("g_v_cutoff", config.guards.v_cutoff);
    preferences.putFloat("g_i_guard", config.guards.i_guard);
    preferences.putBool("g_mcb_guard", config.guards.mcb_guard);
    preferences.putBool("at_enabled", config.autotrigger.enabled);
    preferences.putFloat("at_thresh", config.autotrigger.threshold);
    preferences.end();
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) { Serial.printf("Client connected\n"); }
    else if (type == WS_EVT_DISCONNECT) { Serial.printf("Client disconnected\n"); }
    else if (type == WS_EVT_DATA) { handleWebSocketMessage(arg, data, len); }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, (char*)data, len);
    if (error) { return; }

    const char* action = doc["action"];
    if (strcmp(action, "get_config") == 0) {
        StaticJsonDocument<512> response_doc;
        response_doc["action"] = "config_update";
        JsonObject payload = response_doc.createNestedObject("payload");
        JsonObject guards = payload.createNestedObject("guards");
        guards["v_cutoff"] = config.guards.v_cutoff;
        guards["i_guard"] = config.guards.i_guard;
        guards["mcb_guard"] = config.guards.mcb_guard;
        JsonObject autotrigger = payload.createNestedObject("autotrigger");
        autotrigger["enabled"] = config.autotrigger.enabled;
        autotrigger["threshold"] = config.autotrigger.threshold;
        String response;
        serializeJson(response_doc, response);
        ws.textAll(response);
    } else if (strcmp(action, "set_config") == 0) {
        JsonObject payload = doc["payload"];
        if (payload.containsKey("guards")) {
            config.guards.v_cutoff = payload["guards"]["v_cutoff"];
            config.guards.i_guard = payload["guards"]["i_guard"];
            config.guards.mcb_guard = payload["guards"]["mcb_guard"];
        }
        if (payload.containsKey("autotrigger")) {
            config.autotrigger.enabled = payload["autotrigger"]["enabled"];
            config.autotrigger.threshold = payload["autotrigger"]["threshold"];
        }
        saveConfig();
        // Kirim balik config yang sudah disimpan untuk konfirmasi
        handleWebSocketMessage(arg, (uint8_t*)"{\"action\":\"get_config\"}", 21);
    } else if (strcmp(action, "weld_trigger") == 0){
        webTriggerActive = doc["value"];
    }
}

void performWeld() {
    if(isWelding) return;
    float current_voltage = read_AC_RMS_Voltage();
    if (current_voltage < config.guards.v_cutoff) {
        ws.textAll("{\"action\":\"status_update\", \"status\":\"ERROR: V-Cutoff!\"}");
        delay(2000);
        ws.textAll("{\"action\":\"status_update\", \"status\":\"Ready\"}");
        webTriggerActive = false;
        return;
    }
    isWelding = true;
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Mengelas...\"}");
    if (config.guards.mcb_guard) {
        digitalWrite(DEFAULT_PIN_SSR, HIGH);
        delay(2);
        digitalWrite(DEFAULT_PIN_SSR, LOW);
        delay(100);
    }
    digitalWrite(DEFAULT_PIN_SSR, HIGH);
    delay(timePulse);
    digitalWrite(DEFAULT_PIN_SSR, LOW);
    isWelding = false;
    webTriggerActive = false;
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Selesai!\"}");
    delay(1000);
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Ready\"}");
}

float read_AC_RMS_Voltage() {
    unsigned long startMillis = millis();
    unsigned long peak_adc_val = 0;
    while(millis() - startMillis < 40) {
        int rawADC = analogRead(DEFAULT_PIN_ZMPT);
        if (rawADC > peak_adc_val) peak_adc_val = rawADC;
    }
    float rms_voltage = (peak_adc_val - adc_zero_point_voltage) * VOLTAGE_CALIBRATION / 4095.0;
    return (rms_voltage > 0) ? rms_voltage : 0.0;
}

float read_AC_RMS_Current() {
    unsigned long startMillis = millis();
    float sum_sq_current = 0.0;
    int samples = 0;
    while(millis() - startMillis < 40) {
        int rawADC = analogRead(DEFAULT_PIN_ACS);
        float voltage = (rawADC - adc_zero_point_current) * (3.3 / 4095.0);
        float current = voltage / ACS712_SENSITIVITY;
        sum_sq_current += current * current;
        samples++;
    }
    if (samples == 0) return 0.0;
    return sqrt(sum_sq_current / samples);
}
