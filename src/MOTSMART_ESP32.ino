#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h> // Library baru untuk penyimpanan permanen

// --- OBJEK UNTUK PENYIMPANAN ---
Preferences preferences;

// --- KONFIGURASI JARINGAN ---
const char* ssid = "GeminiSpot_WIFI";
const char* password = "password123";

// --- KONFIGURASI PIN (DEFAULT) ---
#define DEFAULT_PIN_SSR 23
#define DEFAULT_PIN_FOOTSWITCH 34
#define DEFAULT_PIN_ACS 35
#define DEFAULT_PIN_ZMPT 32

// --- OBJEK SERVER ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- STRUKTUR DATA KONFIGURASI ---
struct Config {
    struct Guards {
        float v_cutoff = 180.0;
        float i_guard = 25.0;
        bool mcb_guard = true;
    } guards;
    // ... struct lain akan ditambahkan di sini (autotrigger, pins, dll)
} config;

// --- VARIABEL RUNTIME ---
enum WeldMode { TIME_MODE, ENERGY_MODE, DUAL_PULSE_MODE } currentMode = TIME_MODE;
long timePulse = 20;
// ... variabel lain ...
bool isWelding = false;
volatile bool webTriggerActive = false;
int adc_zero_point_current = 2048;
int adc_zero_point_voltage = 2048;

// --- HALAMAN WEB BARU (DENGAN PANEL GUARDS AKTIF) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>GeminiSpot W-Series</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body{font-family:Arial,sans-serif;background-color:#121212;color:#e0e0e0;text-align:center;margin:0;padding:0}.container{max-width:400px;margin:0 auto;padding:10px}.panel{background-color:#1e1e1e;padding:15px;margin-bottom:15px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.5)}h1,h2{color:#bb86fc;margin-top:0}.status{font-size:1.5em;font-weight:700;color:#03dac6;margin:10px 0}button{background-color:#333;border:1px solid #bb86fc;color:#bb86fc;padding:10px;margin:5px;border-radius:5px;cursor:pointer}button.active{background-color:#bb86fc;color:#121212}input,label{color:#e0e0e0;background-color:#333;border:1px solid #999;border-radius:4px;padding:8px}
    </style>
</head>
<body>
    <div class="container">
        <h1>GeminiSpot W-Series</h1>
        <div class="panel">
            <h2>STATUS</h2>
            <div id="status" class="status">Connecting...</div>
        </div>
        
        <div id="guards_panel" class="panel">
            <h2>Guards (Keamanan)</h2>
            <p><label for="v_cutoff_val">V-Cutoff (V): </label><input type="number" id="v_cutoff_val" step="1"></p>
            <p><label for="i_guard_val">I-Guard (A): </label><input type="number" id="i_guard_val" step="0.5"></p>
            <p><label for="mcb_guard_val">MCB Guard: </label><input type="checkbox" id="mcb_guard_val"></p>
            <button onclick="setGuards()">Simpan Guards</button>
        </div>

        </div>

    <script>
        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        window.addEventListener('load', onload);

        function onload(event) { initWebSocket(); }

        function initWebSocket() {
            websocket = new WebSocket(gateway);
            websocket.onopen = onOpen;
            websocket.onclose = onClose;
            websocket.onmessage = onMessage;
        }

        function onOpen(event) {
            document.getElementById('status').innerText = "Connected";
            websocket.send('{"action":"get_config"}');
        }

        function onClose(event) {
            document.getElementById('status').innerText = "Closed";
            setTimeout(initWebSocket, 2000);
        }

        function onMessage(event) {
            var data = JSON.parse(event.data);
            if (data.action === 'config_update') {
                // Update UI dengan config dari ESP32
                let guards = data.payload.guards;
                document.getElementById('v_cutoff_val').value = guards.v_cutoff;
                document.getElementById('i_guard_val').value = guards.i_guard;
                document.getElementById('mcb_guard_val').checked = guards.mcb_guard;
                document.getElementById('status').innerText = "Ready";
            } else if (data.action === 'status_update') {
                document.getElementById('status').innerText = data.status;
            }
        }

        function setGuards() {
            let msg = {
                action: "set_config",
                payload: {
                    guards: {
                        v_cutoff: parseFloat(document.getElementById('v_cutoff_val').value),
                        i_guard: parseFloat(document.getElementById('i_guard_val').value),
                        mcb_guard: document.getElementById('mcb_guard_val').checked
                    }
                }
            };
            websocket.send(JSON.stringify(msg));
            document.getElementById('status').innerText = "Menyimpan...";
        }
    </script>
</body>
</html>
)rawliteral";

// --- DEKLARASI FUNGSI ---
void loadConfig();
void saveConfig();
void performWeld();
float read_AC_RMS_Voltage();
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

void setup() {
  Serial.begin(115200);
  loadConfig(); // Muat konfigurasi tersimpan saat startup

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
  Serial.print("AP IP address: "); Serial.println(WiFi.softAPIP());
  
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.begin();
  Serial.println("GeminiSpot W-Series W2 Initialized.");
}

void loop() {
  if (digitalRead(DEFAULT_PIN_FOOTSWITCH) == LOW && !isWelding) {
    performWeld();
  }
  ws.cleanupClients();
}

void loadConfig() {
    preferences.begin("geminispot", false);
    config.guards.v_cutoff = preferences.getFloat("g_v_cutoff", 180.0);
    config.guards.i_guard = preferences.getFloat("g_i_guard", 25.0);
    config.guards.mcb_guard = preferences.getBool("g_mcb_guard", true);
    preferences.end();
}

void saveConfig() {
    preferences.begin("geminispot", false);
    preferences.putFloat("g_v_cutoff", config.guards.v_cutoff);
    preferences.putFloat("g_i_guard", config.guards.i_guard);
    preferences.putBool("g_mcb_guard", config.guards.mcb_guard);
    preferences.end();
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket client #%u connected\n", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        handleWebSocketMessage(arg, data, len);
    }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, (char*)data, len);
        if (error) { Serial.println(error.c_str()); return; }

        const char* action = doc["action"];
        if (strcmp(action, "get_config") == 0) {
            StaticJsonDocument<256> response_doc;
            response_doc["action"] = "config_update";
            JsonObject payload = response_doc.createNestedObject("payload");
            JsonObject guards = payload.createNestedObject("guards");
            guards["v_cutoff"] = config.guards.v_cutoff;
            guards["i_guard"] = config.guards.i_guard;
            guards["mcb_guard"] = config.guards.mcb_guard;
            
            String response;
            serializeJson(response_doc, response);
            ws.textAll(response);
        } else if (strcmp(action, "set_config") == 0) {
            JsonObject payload = doc["payload"];
            if (payload.containsKey("guards")) {
                config.guards.v_cutoff = payload["guards"]["v_cutoff"];
                config.guards.i_guard = payload["guards"]["i_guard"];
                config.guards.mcb_guard = payload["guards"]["mcb_guard"];
                saveConfig();
                // Kirim balik config yang sudah disimpan untuk konfirmasi
                handleWebSocketMessage(arg, (uint8_t*)"{\"action\":\"get_config\"}", strlen("{\"action\":\"get_config\"}"));
            }
        }
    }
}

void performWeld() {
    if(isWelding) return;

    // --- LOGIKA GUARDS DIMULAI DI SINI ---
    float current_voltage = read_AC_RMS_Voltage();
    if (current_voltage < config.guards.v_cutoff) {
        Serial.printf("V-Cutoff triggered! Voltage: %.1fV, Required: %.1fV\n", current_voltage, config.guards.v_cutoff);
        ws.textAll("{\"action\":\"status_update\", \"status\":\"ERROR: V-Cutoff!\"}");
        delay(2000);
        ws.textAll("{\"action\":\"status_update\", \"status\":\"Ready\"}");
        return;
    }

    isWelding = true;
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Mengelas...\"}");
    
    // MCB Guard: kirim pulsa pendek
    if (config.guards.mcb_guard) {
        digitalWrite(DEFAULT_PIN_SSR, HIGH);
        delay(2); // Pulsa pre-charge 2ms
        digitalWrite(DEFAULT_PIN_SSR, LOW);
        delay(100); // Jeda sebelum pulsa utama
    }

    // ... sisa logika las akan ditempatkan di sini, menggunakan config.guards.i_guard ...
    // Untuk saat ini, kita simulasikan las selesai
    delay(500); // Simulasi las

    digitalWrite(DEFAULT_PIN_SSR, LOW);
    isWelding = false;
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Selesai!\"}");
    delay(1000);
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Ready\"}");
}

// ... (sisa fungsi read_AC_RMS_Voltage, dll. tetap sama)
float read_AC_RMS_Voltage() { return 220.0; } // Placeholder
