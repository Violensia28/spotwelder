#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>

// --- KONFIGURASI PERANGKAT KERAS ---
// Ganti nomor pin sesuai dengan koneksi hardware Anda
const int FOOTSWITCH_PIN = 4;  // Pin untuk input footswitch
const int WELD_TRIGGER_PIN = 5; // Pin output untuk trigger MOSFET/SSR

// --- PENGATURAN JARINGAN ---
const char* ssid = "GeminiSpot W-Series"; // Nama jaringan WiFi yang akan dibuat
const char* password = NULL; // Kosongkan untuk jaringan terbuka

// --- PENGATURAN SISTEM ---
#define MAX_PRESETS 10 // Jumlah slot preset yang tersedia
#define EEPROM_SIZE (sizeof(Preset) * MAX_PRESETS) + 1 // Kalkulasi ukuran EEPROM

// Struktur untuk menyimpan data preset
struct Preset {
  char name[20];
  int pulseDuration;
  bool isUsed;
};

// Variabel global
Preset presets[MAX_PRESETS];
int currentPulseDuration = 20; // Durasi pulsa default dalam milidetik (ms)
enum State { STATE_READY, STATE_WELDING };
State currentState = STATE_READY;

// Objek Server
AsyncWebServer server(80);

// --- FUNGSI-FUNGSI MANAJEMEN PRESET (EEPROM) ---

void savePresetsToEEPROM() {
  EEPROM.put(0, presets);
  EEPROM.commit();
}

void loadPresetsFromEEPROM() {
  EEPROM.get(0, presets);
  // Inisialisasi jika EEPROM masih kosong/baru
  if (EEPROM.read(sizeof(presets)) != 'C') {
    for (int i = 0; i < MAX_PRESETS; i++) {
      presets[i].isUsed = false;
      strcpy(presets[i].name, "Kosong");
      presets[i].pulseDuration = 0;
    }
    EEPROM.write(sizeof(presets), 'C'); // Tandai bahwa EEPROM sudah diinisialisasi
