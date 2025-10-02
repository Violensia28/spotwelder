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
    savePresetsToEEPROM();
  }
}

// --- KODE ANTARMUKA WEB (HTML, CSS, JS) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>GeminiSpot Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background-color: #121212; color: #E0E0E0; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }
    .container { width: 100%; max-width: 400px; background-color: #1E1E1E; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.3); }
    h2 { color: #BB86FC; text-align: center; }
    .card { background-color: #2C2C2C; padding: 15px; border-radius: 8px; margin-bottom: 20px; }
    .status { font-size: 1.2em; font-weight: bold; text-align: center; padding: 10px; border-radius: 5px; }
    .status.ready { background-color: #03DAC6; color: #121212; }
    .status.welding { background-color: #CF6679; color: #121212; }
    label { display: block; margin-bottom: 5px; color: #B0B0B0; }
    input[type="range"], input[type="text"], select { width: 100%; padding: 8px; margin-bottom: 10px; border-radius: 5px; border: 1px solid #444; background-color: #333; color: #E0E0E0; box-sizing: border-box; }
    input[type="range"] { padding: 0; }
    button { width: 100%; padding: 12px; background-color: #6200EE; color: white; border: none; border-radius: 5px; font-size: 1em; cursor: pointer; transition: background-color 0.2s; }
    button:hover { background-color: #3700B3; }
    .grid-container { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    #pulseValue { font-weight: bold; color: #03DAC6; }
  </style>
</head>
<body>
  <div class="container">
    <h2>GeminiSpot W-Series</h2>
    
    <div class="card">
      <label>STATUS</label>
      <div id="status" class="status">Connecting...</div>
    </div>

    <div class="card">
      <label for="pulseSlider">Pulse Duration (ms): <span id="pulseValue">...</span></label>
      <input type="range" id="pulseSlider" min="1" max="500" onchange="updatePulse()">
    </div>

    <div class="card">
      <h4>Slots / Presets</h4>
      <div class="grid-container">
        <select id="presetSelector"></select>
        <button onclick="loadPreset()">Muat</button>
      </div>
      <br>
      <div class="grid-container">
        <input type="text" id="presetName" placeholder="Nama Preset Baru">
        <button onclick="savePreset()">Simpan</button>
      </div>
    </div>
  </div>

  <script>
    function updatePulse() {
      var slider = document.getElementById("pulseSlider");
      document.getElementById("pulseValue").innerText = slider.value;
      fetch('/set?pulse=' + slider.value);
    }

    function loadPreset() {
      var selector = document.getElementById("presetSelector");
      fetch('/load?slot=' + selector.value)
        .then(response => {
            if (!response.ok) { throw new Error('Network response was not ok'); }
            return response.json();
        })
        .then(data => {
            updateUI(data);
            alert("Preset '" + data.presets[data.slot].name + "' dimuat!");
        })
        .catch(error => console.error('Error loading preset:', error));
    }

    function savePreset() {
      var name = document.getElementById("presetName").value;
      if (name.trim() === "") {
        alert("Nama preset tidak boleh kosong!");
        return;
      }
      fetch('/save?name=' + encodeURIComponent(name))
        .then(response => response.text())
        .then(text => {
          alert(text);
          fetchData(); // Refresh data untuk update list preset
          document.getElementById("presetName").value = "";
        })
        .catch(error => console.error('Error saving preset:', error));
    }
    
    function updateUI(data) {
        // Update Status
        var statusDiv = document.getElementById("status");
        statusDiv.innerText = data.status;
        statusDiv.className = 'status ' + (data.status === "Ready" ? 'ready' : 'welding');
        
        // Update Slider
        document.getElementById("pulseSlider").value = data.pulse;
        document.getElementById("pulseValue").innerText = data.pulse;

        // Update Preset Selector
        var selector = document.getElementById("presetSelector");
        var currentSelection = selector.value;
        selector.innerHTML = ""; // Clear
        for (var i = 0; i < data.presets.length; i++) {
            var option = document.createElement("option");
            option.value = i;
            option.text = "Slot " + (i + 1) + ": " + (data.presets[i].isUsed ? data.presets[i].name : "(Kosong)");
            selector.add(option);
        }
        selector.value = currentSelection;
    }

    function fetchData() {
        fetch('/status')
            .then(response => {
                if (!response.ok) { throw new Error('Network response was not ok'); }
                return response.json();
            })
            .then(data => {
                updateUI(data);
            })
            .catch(error => {
                console.error('Error fetching data:', error);
                document.getElementById("status").innerText = "Disconnected";
                document.getElementById("status").className = 'status welding';
            });
    }

    // Panggil fetchData pertama kali dan set interval
    document.addEventListener('DOMContentLoaded', function() {
        fetchData();
        setInterval(fetchData, 1000); // Update data setiap 1 detik
    });
  </script>
</body>
</html>
)rawliteral";


// --- FUNGSI UTAMA (SETUP & LOOP) ---

void setup() {
  Serial.begin(115200);

  // Inisialisasi pin
  pinMode(WELD_TRIGGER_PIN, OUTPUT);
  digitalWrite(WELD_TRIGGER_PIN, LOW);
  pinMode(FOOTSWITCH_PIN, INPUT_PULLUP); // Gunakan internal pull-up

  // Inisialisasi EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadPresetsFromEEPROM();

  // Inisialisasi WiFi sebagai Access Point
  Serial.println("Starting WiFi AP...");
  WiFi.softAP(ssid, password);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // --- DEFINISI ENDPOINT SERVER WEB ---
  
  // Halaman utama
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Endpoint untuk mendapatkan status & data terbaru (format JSON)
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"status\":\"" + String(currentState == STATE_READY ? "Ready" : "Welding...") + "\",";
    json += "\"pulse\":" + String(currentPulseDuration) + ",";
    json += "\"slot\":" + String(0) + ","; // Default slot
    json += "\"presets\":[";
    for(int i=0; i<MAX_PRESETS; i++){
      json += "{";
      json += "\"name\":\"" + String(presets[i].name) + "\",";
      json += "\"pulse\":" + String(presets[i].pulseDuration) + ",";
      json += "\"isUsed\":" + String(presets[i].isUsed ? "true" : "false");
      json += "}";
      if(i < MAX_PRESETS - 1) json += ",";
    }
    json += "]}";
    request->send(200, "application/json", json);
  });

  // Endpoint untuk mengatur durasi pulsa
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("pulse")) {
      currentPulseDuration = request->getParam("pulse")->value().toInt();
      request->send(200, "text/plain", "Pulse set to " + String(currentPulseDuration));
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Endpoint untuk menyimpan preset
  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("name")) {
      String name = request->getParam("name")->value();
      int slotToSave = -1;
      for(int i=0; i<MAX_PRESETS; i++){
        if(!presets[i].isUsed){
          slotToSave = i;
          break;
        }
      }

      if(slotToSave != -1){
        strncpy(presets[slotToSave].name, name.c_str(), 20);
        presets[slotToSave].pulseDuration = currentPulseDuration;
        presets[slotToSave].isUsed = true;
        savePresetsToEEPROM();
        request->send(200, "text/plain", "Preset disimpan di Slot " + String(slotToSave + 1));
      } else {
        request->send(200, "text/plain", "Semua slot preset sudah penuh!");
      }
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Endpoint untuk memuat preset
  server.on("/load", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("slot")) {
      int slot = request->getParam("slot")->value().toInt();
      if(slot >= 0 && slot < MAX_PRESETS && presets[slot].isUsed){
        currentPulseDuration = presets[slot].pulseDuration;
        
        // Kirim balik data terbaru setelah load
        String json = "{";
        json += "\"status\":\"Ready\",";
        json += "\"pulse\":" + String(currentPulseDuration) + ",";
        json += "\"slot\":" + String(slot) + ",";
        json += "\"presets\":[";
        for(int i=0; i<MAX_PRESETS; i++){
          json += "{\"name\":\"" + String(presets[i].name) + "\",\"isUsed\":" + String(presets[i].isUsed ? "true" : "false") + "}";
          if(i < MAX_PRESETS - 1) json += ",";
        }
        json += "]}";
        request->send(200, "application/json", json);
      } else {
        request->send(404, "text/plain", "Slot not found or empty.");
      }
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Mulai server
  server.begin();
}

void loop() {
  // Logika state machine untuk pengelasan
  switch (currentState) {
    case STATE_READY:
      // Jika footswitch ditekan (LOW karena INPUT_PULLUP)
      if (digitalRead(FOOTSWITCH_PIN) == LOW) {
        currentState = STATE_WELDING;
      }
      break;

    case STATE_WELDING:
      Serial.print("Welding for ");
      Serial.print(currentPulseDuration);
      Serial.println(" ms...");
      
      // Aktifkan trigger
      digitalWrite(WELD_TRIGGER_PIN, HIGH);
      // Tunggu sesuai durasi pulsa
      delay(currentPulseDuration);
      // Matikan trigger
      digitalWrite(WELD_TRIGGER_PIN, LOW);
      
      Serial.println("Weld complete.");
      
      // Kembali ke state ready
      currentState = STATE_READY;
      
      // Tambahkan jeda singkat untuk mencegah pemicuan ganda jika footswitch masih tertekan
      delay(200); 
      break;
  }
}
