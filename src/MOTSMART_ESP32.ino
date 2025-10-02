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

// --- PENGATURAN KESELAMATAN & SENSOR ---
#define MAX_PRIMARY_CURRENT 25.0f
#define ACS712_SENSITIVITY 0.066f
#define VOLTAGE_CALIBRATION 1300.0f

// --- STRUKTUR DATA ---
struct Config {
    // ... (struct Config sama seperti sebelumnya)
} current_config; // Sekarang kita beri nama 'current_config'

struct Slot {
    char name[32];
    Config config;
};
Slot slots[99];
int active_slot = -1; // -1 berarti tidak ada slot yang aktif (pengaturan manual)

// --- VARIABEL RUNTIME ---
// ... (variabel runtime lainnya)
bool isWelding = false;

// --- HALAMAN WEB (DENGAN PANEL SLOTS) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>GeminiSpot W-Series</title>
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
        // ... (JavaScript di-update secara signifikan untuk handle slots)
        function onMessage(event) {
            var data = JSON.parse(event.data);
            if (data.action === 'full_config') {
                updateUI(data.payload);
            } else if (data.action === 'slots_list') {
                populateSlots(data.payload);
            } // ... etc
        }

        function populateSlots(slots) {
            let selector = document.getElementById('slot_selector');
            selector.innerHTML = '';
            for (let i = 0; i < slots.length; i++) {
                let option = document.createElement('option');
                option.value = i;
                option.innerText = `Slot ${i + 1}: ${slots[i].name || '(Kosong)'}`;
                selector.appendChild(option);
            }
        }

        function loadSlot() {
            let slot_id = document.getElementById('slot_selector').value;
            websocket.send(`{"action":"load_slot", "slot_id":${slot_id}}`);
        }

        function saveSlot() {
            let slot_id = document.getElementById('slot_selector').value;
            let slot_name = document.getElementById('slot_name').value;
            // Kumpulkan semua config saat ini dari UI...
            let current_config = { /* ... */ };
            let msg = {
                action: "save_slot",
                slot_id: parseInt(slot_id),
                name: slot_name,
                payload: current_config
            };
            websocket.send(JSON.stringify(msg));
        }

        // ... (Sisa JavaScript lainnya)
    </script>
</body>
</html>
)rawliteral";

// --- DEKLARASI FUNGSI ---
void loadSlots();
void saveSlot(int slot_id, JsonObject payload);
// ... (deklarasi fungsi lainnya)

void setup() {
  Serial.begin(115200);
  loadSlots(); // Muat semua 99 slot dari memori saat startup
  
  // ... (sisa fungsi setup sama seperti sebelumnya)
}

void loop() {
  // ... (fungsi loop sama seperti sebelumnya)
}

// --- FUNGSI MANAJEMEN SLOT ---

void loadSlots() {
    preferences.begin("geminispot", true); // Buka NVS dalam mode read-only
    // Untuk simplifikasi, kita muat satu per satu. Di aplikasi nyata, ini bisa dioptimalkan.
    for (int i = 0; i < 99; i++) {
        String key_name = "s" + String(i) + "_name";
        String slotName = preferences.getString(key_name.c_str(), "");
        strncpy(slots[i].name, slotName.c_str(), 32);

        // Muat juga data config untuk setiap slot...
        // preferences.getBytes(...);
    }
    preferences.end();
}

void saveSlot(int slot_id, JsonObjectConst payload) {
    if (slot_id < 0 || slot_id >= 99) return;

    const char* new_name = payload["name"];
    strncpy(slots[slot_id].name, new_name, 32);

    // Salin data config dari payload ke struct slots[slot_id].config
    // ...

    // Simpan ke NVS
    preferences.begin("geminispot", false);
    String key_name = "s" + String(slot_id) + "_name";
    preferences.putString(key_name.c_str(), slots[slot_id].name);
    // ... simpan juga data config ...
    preferences.end();
}

// --- FUNGSI WEBSOCKET HANDLER (Diperbarui) ---
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    StaticJsonDocument<1024> doc; // Ukuran diperbesar untuk menampung config
    deserializeJson(doc, (char*)data, len);

    const char* action = doc["action"];
    if (strcmp(action, "get_config") == 0) { /* ... kirim config saat ini ... */ }
    else if (strcmp(action, "get_slots") == 0) {
        // Kirim daftar nama semua 99 slot ke UI
        StaticJsonDocument<4096> response_doc; // Ukuran besar untuk daftar slot
        response_doc["action"] = "slots_list";
        JsonArray payload = response_doc.createNestedArray("payload");
        for(int i=0; i<99; i++){
            JsonObject slot_info = payload.createNestedObject();
            slot_info["name"] = slots[i].name;
        }
        String response;
        serializeJson(response_doc, response);
        ws.textAll(response);
    }
    else if (strcmp(action, "load_slot") == 0) {
        int slot_id = doc["slot_id"];
        if (slot_id >= 0 && slot_id < 99) {
            current_config = slots[slot_id].config;
            active_slot = slot_id;
            // Kirim config yang baru dimuat ke UI untuk di-update
        }
    }
    else if (strcmp(action, "save_slot") == 0) {
        int slot_id = doc["slot_id"];
        saveSlot(slot_id, doc);
        // Kirim konfirmasi atau daftar slot terbaru
    }
    // ... (sisa handler)
}
// ... (sisa fungsi lainnya)
