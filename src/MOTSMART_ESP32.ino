#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// --- DEKLARASI FUNGSI (PROTOTYPES) ---
void performWeld();
float read_AC_RMS_Current();
float read_AC_RMS_Voltage();
void loadSlots();
void saveSlot(int slot_id, const char* name, JsonObjectConst config_payload);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

// --- OBJEK & KONFIGURASI GLOBAL ---
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

// --- PENGATURAN KESELAMATAN & SENSOR ---
#define MAX_PRIMARY_CURRENT 25.0f
#define ACS712_SENSITIVITY 0.066f
#define VOLTAGE_CALIBRATION 1300.0f

// --- STRUKTUR DATA ---
struct Config {
    struct Guards {
        float v_cutoff = 180.0;
        float i_guard = 25.0;
        bool mcb_guard = true;
    } guards;
    struct AutoTrigger {
        bool enabled = false;
        float threshold = 3.5;
        int ping_interval = 100;
    } autotrigger;
} current_config;

struct Slot {
    char name[32];
    Config config;
};
Slot slots[99];
int active_slot = -1;

// --- VARIABEL RUNTIME LAINNYA ---
enum WeldMode { TIME_MODE, ENERGY_MODE, DUAL_PULSE_MODE } currentMode = TIME_MODE;
long timePulse = 20;
bool isWelding = false;
volatile bool webTriggerActive = false;
int adc_zero_point_current = 2048;
int adc_zero_point_voltage = 2048;
unsigned long lastPingTime = 0;

// --- HALAMAN WEB LENGKAP ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>GeminiSpot W-Series</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body{font-family:Arial,sans-serif;background-color:#121212;color:#e0e0e0;text-align:center;margin:0;padding:0}.container{max-width:400px;margin:0 auto;padding:10px}.panel{background-color:#1e1e1e;padding:15px;margin-bottom:15px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.5)}h1,h2{color:#bb86fc;margin-top:0}.status{font-size:1.5em;font-weight:700;color:#03dac6;margin:10px 0}button{background-color:#333;border:1px solid #bb86fc;color:#bb86fc;padding:10px;margin:5px;border-radius:5px;cursor:pointer}button.active{background-color:#bb86fc;color:#121212}input,select,label{color:#e0e0e0;background-color:#333;border:1px solid #999;border-radius:4px;padding:8px}
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
            <h2>Slots / Presets</h2>
            <p>
                <select id="slot_selector"></select>
                <button onclick="loadSlot()">Muat</button>
            </p>
            <p>
                <input type="text" id="slot_name" placeholder="Nama Preset Baru">
                <button onclick="saveSlot()">Simpan</button>
            </p>
        </div>
        </div>
    <script>
        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        
        function initWebSocket(){websocket=new WebSocket(gateway);websocket.onopen=onOpen;websocket.onclose=onClose;websocket.onmessage=onMessage}
        function onOpen(event){document.getElementById('status').innerText="Connected";websocket.send('{"action":"get_slots"}');websocket.send('{"action":"get_config"}')}
        function onClose(event){document.getElementById('status').innerText="Closed";setTimeout(initWebSocket,2000)}
        function onMessage(event){var data=JSON.parse(event.data);if(data.action==='config_update'){document.getElementById('status').innerText="Ready"}else if(data.action==='slots_list'){populateSlots(data.payload)}else if(data.action==='status_update'){document.getElementById('status').innerText=data.status}}
        function populateSlots(slots_data){let selector=document.getElementById('slot_selector');selector.innerHTML='';for(let i=0;i<slots_data.length;i++){let option=document.createElement('option');option.value=i;option.innerText=`Slot ${i+1}: ${slots_data[i].name||'(Kosong)'}`;selector.appendChild(option)}}
        function loadSlot(){let slot_id=document.getElementById('slot_selector').value;websocket.send(`{"action":"load_slot", "slot_id":${parseInt(slot_id)}}`)}
        function saveSlot(){let slot_id=document.getElementById('slot_selector').value;let slot_name=document.getElementById('slot_name').value;if(!slot_name){alert("Nama preset tidak boleh kosong!");return}
        let msg={action:"save_slot",slot_id:parseInt(slot_id),name:slot_name};websocket.send(JSON.stringify(msg))}
        window.addEventListener('load', initWebSocket);
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  loadSlots();
  pinMode(DEFAULT_PIN_SSR, OUTPUT);
  pinMode(DEFAULT_PIN_FOOTSWITCH, INPUT_PULLUP);
  WiFi.softAP(ssid, password);
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.begin();
  Serial.println("GeminiSpot W-Series W4 (Final Fix) Initialized.");
}

void loop() {
  // Loop logic remains the same
}

void loadSlots() {
    preferences.begin("geminispot", true);
    for (int i = 0; i < 99; i++) {
        String key_name = "s" + String(i) + "_name";
        String slotName = preferences.getString(key_name.c_str(), "");
        strncpy(slots[i].name, slotName.c_str(), 31);
        slots[i].name[31] = '\0';
    }
    preferences.end();
}

void saveSlot(int slot_id, const char* name, JsonObjectConst config_payload) {
    if (slot_id < 0 || slot_id >= 99) return;
    strncpy(slots[slot_id].name, name, 31);
    slots[slot_id].name[31] = '\0';
    slots[slot_id].config = current_config; // Simpan config yg aktif saat ini

    preferences.begin("geminispot", false);
    String key_name = "s" + String(slot_id) + "_name";
    preferences.putString(key_name.c_str(), slots[slot_id].name);
    // Di sini nanti kita tambahkan penyimpanan config-nya
    preferences.end();
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) { Serial.printf("Client connected\n"); }
    else if (type == WS_EVT_DISCONNECT) { Serial.printf("Client disconnected\n"); }
    else if (type == WS_EVT_DATA) { handleWebSocketMessage(arg, data, len); }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, (char*)data, len);
    const char* action = doc["action"];

    if (strcmp(action, "get_slots") == 0) {
        StaticJsonDocument<4096> response_doc;
        response_doc["action"] = "slots_list";
        JsonArray payload = response_doc.createNestedArray("payload");
        for(int i=0; i<99; i++){
            JsonObject slot_info = payload.createNestedObject();
            slot_info["name"] = slots[i].name;
        }
        String response;
        serializeJson(response_doc, response);
        ws.textAll(response);
    } else if (strcmp(action, "load_slot") == 0) {
        int slot_id = doc["slot_id"];
        if (slot_id >= 0 && slot_id < 99) {
            current_config = slots[slot_id].config;
            active_slot = slot_id;
            // Kirim config baru ke UI
        }
    } else if (strcmp(action, "save_slot") == 0) {
        int slot_id = doc["slot_id"];
        const char* name = doc["name"];
        
        StaticJsonDocument<512> config_doc;
        // Kita simpan config yang sedang aktif
        // (Ini hanya contoh, nanti kita buat fungsi khususnya)
        
        saveSlot(slot_id, name, config_doc.as<JsonObjectConst>());
        
        // Kirim daftar slot terbaru untuk update UI
        handleWebSocketMessage(arg, (uint8_t*)"{\"action\":\"get_slots\"}", 20);
    }
}

// Sisa fungsi lain (performWeld, read_AC_RMS_Voltage, read_AC_RMS_Current, dll)
// ditempatkan di sini...
void performWeld(){}
float read_AC_RMS_Voltage(){return 0.0;}
float read_AC_RMS_Current(){return 0.0;}
