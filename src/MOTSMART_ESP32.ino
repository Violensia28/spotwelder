#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// --- DEKLARASI FUNGSI (PROTOTYPES) ---
void loadConfig();
void saveConfig();
void performWeld();
float read_AC_RMS_Voltage();
float read_AC_RMS_Current();
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

// --- HALAMAN WEB (DENGAN PANEL SLOTS) ---
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
        window.addEventListener('load', onload);

        function onload(
