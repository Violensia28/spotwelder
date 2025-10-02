#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// --- DEKLARASI FUNGSI ---
void performWeld();
// ... (deklarasi fungsi lainnya)
void saveSlot(int slot_id, const char* name, JsonObjectConst config_payload); // Tanda tangan fungsi diubah

// ... (semua #define dan struct Config/Slot sama seperti sebelumnya)

// --- HALAMAN WEB ---
// ... (Konten HTML/CSS/JS sama persis seperti respons W4 sebelumnya)

// --- SETUP & LOOP ---
void setup() { /* ... (fungsi setup sama seperti sebelumnya) ... */ }
void loop() { /* ... (fungsi loop sama seperti sebelumnya) ... */ }

// --- FUNGSI MANAJEMEN SLOT (DENGAN PERBAIKAN) ---
void loadSlots() {
    preferences.begin("geminispot", true);
    for (int i = 0; i < 99; i++) {
        String key_name = "s" + String(i) + "_name";
        String slotName = preferences.getString(key_name.c_str(), "");
        strncpy(slots[i].name, slotName.c_str(), 31);
        slots[i].name[31] = '\0'; // Pastikan null-terminated

        String key_conf = "s" + String(i) + "_conf";
        size_t conf_len = preferences.getBytesLength(key_conf.c_str());
        if (conf_len > 0) {
            char buffer[512];
            preferences.getBytes(key_conf.c_str(), buffer, conf_len);
            StaticJsonDocument<512> doc;
            deserializeJson(doc, buffer);
            // Salin data dari JSON ke struct config slot
            slots[i].config.guards.v_cutoff = doc["guards"]["v_cutoff"];
            // ... (lanjutkan untuk semua item config)
        }
    }
    preferences.end();
}

// --- PERUBAHAN DI SINI ---
// Fungsi saveSlot sekarang menerima nama dan payload config secara terpisah
void saveSlot(int slot_id, const char* name, JsonObjectConst config_payload) {
    if (slot_id < 0 || slot_id >= 99) return;

    // Simpan nama
    strncpy(slots[slot_id].name, name, 31);
    slots[slot_id].name[31] = '\0';

    // Salin data config dari payload ke struct
    slots[slot_id].config.guards.v_cutoff = config_payload["guards"]["v_cutoff"];
    slots[slot_id].config.guards.i_guard = config_payload["guards"]["i_guard"];
    slots[slot_id].config.guards.mcb_guard = config_payload["guards"]["mcb_guard"];
    slots[slot_id].config.autotrigger.enabled = config_payload["autotrigger"]["enabled"];
    slots[slot_id].config.autotrigger.threshold = config_payload["autotrigger"]["threshold"];
    
    // Simpan ke NVS
    preferences.begin("geminispot", false);
    String key_name = "s" + String(slot_id) + "_name";
    preferences.putString(key_name.c_str(), slots[slot_id].name);

    String key_conf = "s" + String(slot_id) + "_conf";
    StaticJsonDocument<512> doc;
    doc.set(config_payload);
    char buffer[512];
    size_t len = serializeJson(doc, buffer);
    preferences.putBytes(key_conf.c_str(), buffer, len);
    
    preferences.end();
}


// --- FUNGSI WEBSOCKET HANDLER (DENGAN PERBAIKAN) ---
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, (char*)data, len);
    if (error) { return; }

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
    }
    else if (strcmp(action, "load_slot") == 0) {
        int slot_id = doc["slot_id"];
        if (slot_id >= 0 && slot_id < 99) {
            current_config = slots[slot_id].config;
            active_slot = slot_id;
            // Kirim config yang baru dimuat ke UI untuk di-update
            // (Mirip dengan get_config)
        }
    }
    else if (strcmp(action, "save_slot") == 0) {
        // --- PERUBAHAN DI SINI ---
        int slot_id = doc["slot_id"];
        const char* name = doc["name"];
        JsonObjectConst config_payload = doc["payload"];
        
        // Panggil fungsi saveSlot dengan argumen yang benar
        saveSlot(slot_id, name, config_payload);
        
        // Kirim konfirmasi atau daftar slot terbaru
        handleWebSocketMessage(arg, (uint8_t*)"{\"action\":\"get_slots\"}", 20);
    }
    // ... (sisa handler seperti get_config, set_config, dll)
}

// ... (sisa fungsi lainnya seperti onEvent, performWeld, setup, loop tidak berubah signifikan)
