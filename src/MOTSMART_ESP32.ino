#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// --- KONFIGURASI JARINGAN ---
const char* ssid = "GeminiSpot_WIFI";
const char* password = "password123";

// --- KONFIGURASI PIN (DEFAULT, NANTI BISA DIUBAH DARI WEB UI) ---
#define DEFAULT_PIN_SSR 23
#define DEFAULT_PIN_FOOTSWITCH 34
#define DEFAULT_PIN_ACS 35
#define DEFAULT_PIN_ZMPT 32

// --- OBJEK SERVER ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- VARIABEL & STRUCT UNTUK KONFIGURASI ---
// Ini adalah fondasi untuk semua fitur baru
struct Config {
    struct {
        bool zmpt_enabled = true;
        bool acs712_enabled = true;
    } sensors;
    struct {
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

// --- HALAMAN WEB BARU SESUAI SPESIFIKASI W-SERIES ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>GeminiSpot W-Series</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        /* ... CSS LENGKAP UNTUK UI BARU ... */
        body { font-family: sans-serif; background-color: #121212; color: #e0e0e0; }
        /* ... (Gaya lainnya tetap sama, ditambahkan beberapa untuk menu baru) ... */
    </style>
</head>
<body>
    <div class="container">
        <h1>GeminiSpot W-Series</h1>
        <div id="main-menu">
            </div>
        </div>
    <script>
        // ... JavaScript akan dirombak total untuk menggunakan JSON ...
        // Contoh pengiriman data baru:
        function setGuards() {
            let v_cutoff = document.getElementById('v_cutoff_val').value;
            let i_guard = document.getElementById('i_guard_val').value;
            let msg = {
                action: "set_config",
                payload: {
                    guards: {
                        v_cutoff: parseFloat(v_cutoff),
                        i_guard: parseFloat(i_guard)
                    }
                }
            };
            websocket.send(JSON.stringify(msg));
        }
    </script>
</body>
</html>
)rawliteral";
// (Catatan: Konten HTML/JS lengkap akan sangat panjang, ini hanya ilustrasi konsep)


// --- DEKLARASI FUNGSI ---
void performWeld();
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
// ... fungsi-fungsi lain ...


void setup() {
  Serial.begin(115200);

  // Inisialisasi pin (nanti akan diambil dari config)
  pinMode(DEFAULT_PIN_SSR, OUTPUT);
  digitalWrite(DEFAULT_PIN_SSR, LOW);
  pinMode(DEFAULT_PIN_FOOTSWITCH, INPUT_PULLUP);

  // Kalibrasi sensor jika aktif di config
  if(config.sensors.acs712_enabled) {
      // ... logika kalibrasi ACS712 ...
  }
  if(config.sensors.zmpt_enabled) {
      // ... logika kalibrasi ZMPT101B ...
  }
  
  // Setup WiFi, WebSocket, dan Web Server
  WiFi.softAP(ssid, password);
  Serial.print("AP IP address: "); Serial.println(WiFi.softAPIP());
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.begin();

  Serial.println("GeminiSpot W-Series W1 Initialized.");
}


void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (char*)data, len);

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            return;
        }

        const char* action = doc["action"];

        if (strcmp(action, "get_config") == 0) {
            // Kirim seluruh konfigurasi saat ini ke UI
        } 
        else if (strcmp(action, "set_config") == 0) {
            // Perbarui konfigurasi berdasarkan data dari UI
            if (doc.containsKey("payload") && doc["payload"].containsKey("guards")) {
                config.guards.v_cutoff = doc["payload"]["guards"]["v_cutoff"];
                config.guards.i_guard = doc["payload"]["guards"]["i_guard"];
                // ... simpan ke NVS (memori permanen) ...
            }
        }
        else if (strcmp(action, "weld_trigger") == 0) {
            webTriggerActive = doc["value"];
        }
        // ... handle action lain (slots, pin_mapper, dll)
    }
}


void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT: Serial.printf("WebSocket client #%u connected\n", client->id()); break;
        case WS_EVT_DISCONNECT: Serial.printf("WebSocket client #%u disconnected\n", client->id()); break;
        case WS_EVT_DATA: handleWebSocketMessage(arg, data, len); break;
        case WS_EVT_PONG: case WS_EVT_ERROR: break;
    }
}


void loop() {
  if (digitalRead(DEFAULT_PIN_FOOTSWITCH) == LOW && !isWelding) {
    performWeld();
  }
  if (webTriggerActive && !isWelding){
    performWeld();
  }
  ws.cleanupClients();
}

void performWeld() {
    // ... Logika pengelasan akan dimodifikasi untuk membaca config
    // misal, cek if(config.guards.v_cutoff_enabled) sebelum mengevaluasi ...
    // Untuk saat ini, kita biarkan sama dulu.
}

