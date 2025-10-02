#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

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

// --- HALAMAN WEB BARU (DENGAN SEMUA FITUR) ---
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

        window.addEventListener('load', onload);

        function onload(event) {
            initWebSocket();
            initAudio();
            document.getElementById('weld_btn').addEventListener('mousedown', () => triggerWeld(true));
            document.getElementById('weld_btn').addEventListener('mouseup', () => triggerWeld(false));
            document.getElementById('weld_btn').addEventListener('touchstart', (e) => { e.preventDefault(); triggerWeld(true); });
            document.getElementById('weld_btn').addEventListener('touchend', (e) => { e.preventDefault(); triggerWeld(false); });
        }

        function initAudio() {
            try {
                audioCtx = new (window.AudioContext || window.webkitAudioContext)();
            } catch (e) {
                console.log('Web Audio API is not supported in this browser');
            }
        }

        function playBeep() {
            if (!audioCtx || oscillator) return;
            oscillator = audioCtx.createOscillator();
            oscillator.type = 'sine';
            oscillator.frequency.setValueAtTime(880, audioCtx.currentTime); // A5 note
            oscillator.connect(audioCtx.destination);
            oscillator.start();
        }

        function stopBeep() {
            if (oscillator) {
                oscillator.stop();
                oscillator.disconnect();
                oscillator = null;
            }
        }

        function initWebSocket() { /* ... (fungsi ini sama seperti sebelumnya) ... */ }
        function onOpen(event) { /* ... (fungsi ini sama seperti sebelumnya) ... */ }
        function onClose(event) { /* ... (fungsi ini sama seperti sebelumnya) ... */ }
        function onMessage(event) {
            var data = JSON.parse(event.data);
            if (data.action === 'config_update') {
                let guards = data.payload.guards;
                document.getElementById('v_cutoff_val').value = guards.v_cutoff;
                document.getElementById('i_guard_val').value = guards.i_guard;
                document.getElementById('mcb_guard_val').checked = guards.mcb_guard;

                let autotrigger = data.payload.autotrigger;
                document.getElementById('autotrigger_enabled').checked = autotrigger.enabled;
                document.getElementById('autotrigger_threshold').value = autotrigger.threshold;
                
                document.getElementById('status').innerText = "Ready";
            } else if (data.action === 'status_update') {
                document.getElementById('status').innerText = data.status;
            }
        }

        function setConfig(section) { /* ... (fungsi ini sama seperti sebelumnya) ... */ }

        function triggerWeld(start) {
            websocket.send(`{"action":"weld_trigger", "value":${start}}`);
            if (start) { playBeep(); } else { stopBeep(); }
        }
    </script>
</body>
</html>
)rawliteral";


// --- DEKLARASI FUNGSI & FUNGSI LAINNYA ---
// ... (Sebagian besar fungsi lain seperti load/saveConfig, onEvent, handleWebSocketMessage tetap sama)
// ... (Hanya loop() dan performWeld() yang akan dimodifikasi signifikan)

void performWeld();

void setup() { /* ... (fungsi setup sama seperti sebelumnya) ... */ }

void loop() {
  // Cek pemicu fisik (footswitch)
  if (digitalRead(DEFAULT_PIN_FOOTSWITCH) == LOW && !isWelding) {
    performWeld();
  }

  // Cek pemicu dari Web UI
  if (webTriggerActive && !isWelding) {
    performWeld();
  }

  // Logika Auto-Trigger (Metode "Ping")
  if (config.autotrigger.enabled && !isWelding && (millis() - lastPingTime > config.autotrigger.ping_interval)) {
    lastPingTime = millis();
    
    // Kirim pulsa "ping" yang sangat singkat
    digitalWrite(DEFAULT_PIN_SSR, HIGH);
    delayMicroseconds(500); // 0.5 ms pulse
    digitalWrite(DEFAULT_PIN_SSR, LOW);

    // Baca arus dengan cepat setelah ping
    float ping_current = read_AC_RMS_Current();

    // Idle current trafo MOT bisa sekitar 1-2A. Threshold harus di atas itu.
    if (ping_current > config.autotrigger.threshold) {
        Serial.printf("Auto-Trigger fired! Current: %.2fA, Threshold: %.2fA\n", ping_current, config.autotrigger.threshold);
        performWeld();
    }
  }
  
  ws.cleanupClients();
}

void performWeld() {
    if(isWelding) return;

    float current_voltage = read_AC_RMS_Voltage();
    if (current_voltage < config.guards.v_cutoff) {
        ws.textAll("{\"action\":\"status_update\", \"status\":\"ERROR: V-Cutoff!\"}");
        delay(2000); // Tampilkan error selama 2 detik
        ws.textAll("{\"action\":\"status_update\", \"status\":\"Ready\"}");
        webTriggerActive = false; // Reset pemicu web
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

    // ... (Logika las utama akan diimplementasikan penuh di sini)
    // ... Menggunakan i_guard, mode waktu/energi/dual, dll.
    digitalWrite(DEFAULT_PIN_SSR, HIGH);
    delay(timePulse); // Untuk saat ini, kita gunakan mode waktu sederhana
    digitalWrite(DEFAULT_PIN_SSR, LOW);

    isWelding = false;
    webTriggerActive = false; // Reset pemicu web setelah las selesai
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Selesai!\"}");
    delay(1000);
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Ready\"}");
}
// ... (sisa fungsi lain seperti read_AC_RMS_Voltage, handleWebSocketMessage, dll. sama)
