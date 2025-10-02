#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// --- DEKLARASI FUNGSI (PROTOTYPE) ---
void performWeld(); // <--- INI ADALAH PERBAIKANNYA

// --- KONFIGURASI JARINGAN ---
const char* ssid = "GeminiSpot_WIFI";
const char* password = "password123";

// --- KONFIGURASI PIN ---
#define SSR_TRIGGER_PIN 23
#define FOOTSWITCH_PIN 34
#define ACS712_PIN 35
#define ZMPT_PIN 32

// --- PENGATURAN KESELAMATAN & SENSOR ---
#define MAX_PRIMARY_CURRENT 25.0f
#define ACS712_SENSITIVITY 0.066f
#define VOLTAGE_CALIBRATION 1300.0f

// --- OBJEK SERVER ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- VARIABEL GLOBAL ---
enum WeldMode { TIME_MODE, ENERGY_MODE, DUAL_PULSE_MODE } currentMode = TIME_MODE;
long timePulse = 20, energyTarget = 50, dual_pulse1 = 15, dual_pulse2 = 30, dual_delay = 50;
bool isWelding = false;
volatile bool webTriggerActive = false;
int adc_zero_point_current = 2048;
int adc_zero_point_voltage = 2048;

// --- KODE HALAMAN WEB (HTML, CSS, JavaScript) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>GeminiSpot Controller</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; background-color: #121212; color: #e0e0e0; text-align: center; }
        .container { max-width: 400px; margin: 0 auto; padding: 20px; }
        .panel { background-color: #1e1e1e; padding: 20px; margin-bottom: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.5); }
        h1, h2 { color: #bb86fc; }
        .status { font-size: 1.5em; font-weight: bold; color: #03dac6; margin: 10px 0; }
        .mode-btn { background-color: #333; border: 1px solid #bb86fc; color: #bb86fc; padding: 10px; margin: 5px; border-radius: 5px; cursor: pointer; }
        .mode-btn.active { background-color: #bb86fc; color: #121212; }
        input[type=number] { width: 80px; padding: 8px; background-color: #333; border: 1px solid #999; color: #e0e0e0; border-radius: 4px; }
        .weld-btn { background-color: #cf6679; color: white; padding: 20px; font-size: 1.5em; border: none; border-radius: 50%; width: 150px; height: 150px; cursor: pointer; margin-top: 20px; -webkit-user-select: none; user-select: none;}
        .weld-btn:active { background-color: #ff8a80; }
        .result { text-align: left; font-size: 0.9em; }
    </style>
</head>
<body>
    <div class="container">
        <h1>GeminiSpot V2.3</h1>
        <div class="panel">
            <h2>STATUS</h2>
            <div id="status" class="status">Connecting...</div>
        </div>
        <div class="panel">
            <h2>KONTROL</h2>
            <div>
                <button id="btn_time" class="mode-btn active" onclick="setMode('TIME')">Waktu</button>
                <button id="btn_energy" class="mode-btn" onclick="setMode('ENERGY')">Energi</button>
                <button id="btn_dual" class="mode-btn" onclick="setMode('DUAL')">Pulsa Ganda</button>
            </div>
            <div id="control_time">
                <p>Durasi (ms): <input type="number" id="time_val" onchange="setValue()"></p>
            </div>
            <div id="control_energy" style="display:none;">
                <p>Energi (J): <input type="number" id="energy_val" onchange="setValue()"></p>
            </div>
            <div id="control_dual" style="display:none;">
                <p>P1:<input type="number" id="dual1_val" onchange="setValue()"> P2:<input type="number" id="dual2_val" onchange="setValue()"> Delay:<input type="number" id="delay_val" onchange="setValue()"></p>
            </div>
        </div>
        <div class="panel">
            <h2>AKSI</h2>
            <button id="weld_btn" class="weld-btn" onmousedown="triggerWeld(true)" onmouseup="triggerWeld(false)" ontouchstart="triggerWeld(true)" ontouchend="triggerWeld(false)">TAHAN UNTUK LAS</button>
        </div>
        <div class="panel result">
            <h3>Hasil Las Terakhir:</h3>
            <p id="res_voltage">Tegangan: -</p>
            <p id="res_current">Puncak Arus: -</p>
            <p id="res_duration">Durasi: -</p>
            <p id="res_energy">Energi: -</p>
        </div>
    </div>

    <script>
        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        var currentMode = 'TIME';
        window.addEventListener('load', onload);
        function onload(event) { initWebSocket(); }
        function initWebSocket() {
            console.log('Trying to open a WebSocket connection...');
            websocket = new WebSocket(gateway);
            websocket.onopen = onOpen;
            websocket.onclose = onClose;
            websocket.onmessage = onMessage;
        }
        function onOpen(event) {
            console.log('Connection opened');
            document.getElementById('status').innerText = "Connected";
            websocket.send('{"action":"get_state"}');
        }
        function onClose(event) {
            console.log('Connection closed');
            document.getElementById('status').innerText = "Closed";
            setTimeout(initWebSocket, 2000);
        }
        function onMessage(event) {
            var data = JSON.parse(event.data);
            if(data.action === 'state'){
                currentMode = data.mode;
                updateModeUI();
                document.getElementById('time_val').value = data.timePulse;
                document.getElementById('energy_val').value = data.energyTarget;
                document.getElementById('dual1_val').value = data.dual_pulse1;
                document.getElementById('dual2_val').value = data.dual_pulse2;
                document.getElementById('delay_val').value = data.dual_delay;
                document.getElementById('status').innerText = data.status;
            } else if(data.action === 'weld_result'){
                document.getElementById('res_voltage').innerText = `Tegangan: ${data.voltage.toFixed(1)} V`;
                document.getElementById('res_duration').innerText = `Durasi: ${data.duration} ms`;
                document.getElementById('res_current').innerText = `Puncak Arus: ${data.peak_current.toFixed(1)} A`;
                document.getElementById('res_energy').innerText = `Energi: ${data.energy.toFixed(1)} J`;
                document.getElementById('status').innerText = data.status;
            } else if(data.action === 'status_update'){
                document.getElementById('status').innerText = data.status;
            }
        }
        function setMode(mode){
            currentMode = mode;
            updateModeUI();
            websocket.send(`{"action":"set_mode", "value":"${mode}"}`);
        }
        function updateModeUI(){
            document.getElementById('btn_time').classList.toggle('active', currentMode === 'TIME');
            document.getElementById('btn_energy').classList.toggle('active', currentMode === 'ENERGY');
            document.getElementById('btn_dual').classList.toggle('active', currentMode === 'DUAL');
            document.getElementById('control_time').style.display = currentMode === 'TIME' ? 'block' : 'none';
            document.getElementById('control_energy').style.display = currentMode === 'ENERGY' ? 'block' : 'none';
            document.getElementById('control_dual').style.display = currentMode === 'DUAL' ? 'block' : 'none';
        }
        function setValue(){
            var data = { action: 'set_value' };
            data.timePulse = parseInt(document.getElementById('time_val').value);
            data.energyTarget = parseInt(document.getElementById('energy_val').value);
            data.dual_pulse1 = parseInt(document.getElementById('dual1_val').value);
            data.dual_pulse2 = parseInt(document.getElementById('dual2_val').value);
            data.dual_delay = parseInt(document.getElementById('delay_val').value);
            websocket.send(JSON.stringify(data));
        }
        function triggerWeld(start){ websocket.send(`{"action":"weld_trigger", "value":${start}}`); }
    </script>
</body>
</html>
)rawliteral";

// --- FUNGSI-FUNGSI LOGIKA ---

float read_AC_RMS_Voltage() {
    unsigned long startMillis = millis();
    unsigned long peak_adc_val = 0;
    while(millis() - startMillis < 40) {
        int rawADC = analogRead(ZMPT_PIN);
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
        int rawADC = analogRead(ACS712_PIN);
        float voltage = (rawADC - adc_zero_point_current) * (3.3 / 4095.0);
        float current = voltage / ACS712_SENSITIVITY;
        sum_sq_current += current * current;
        samples++;
    }
    if (samples == 0) return 0.0;
    return sqrt(sum_sq_current / samples);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        String message = (char*)data;
        if (message.indexOf("get_state") > 0) {
            String modeStr = (currentMode == TIME_MODE) ? "TIME" : (currentMode == ENERGY_MODE) ? "ENERGY" : "DUAL";
            String jsonState = "{\"action\":\"state\",\"mode\":\"" + modeStr + "\",\"timePulse\":" + String(timePulse) + ",\"energyTarget\":" + String(energyTarget) + ",\"dual_pulse1\":" + String(dual_pulse1) + ",\"dual_pulse2\":" + String(dual_pulse2) + ",\"dual_delay\":" + String(dual_delay) + ",\"status\":\"SIAP\"}";
            ws.textAll(jsonState);
        } else if (message.indexOf("set_mode") > 0){
            if(message.indexOf("TIME") > 0) currentMode = TIME_MODE;
            else if(message.indexOf("ENERGY") > 0) currentMode = ENERGY_MODE;
            else if(message.indexOf("DUAL") > 0) currentMode = DUAL_PULSE_MODE;
        } else if (message.indexOf("weld_trigger") > 0){
            webTriggerActive = (message.indexOf("true") > 0);
        }
        // Parsing untuk set_value bisa ditambahkan di sini
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT: Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str()); break;
        case WS_EVT_DISCONNECT: Serial.printf("WebSocket client #%u disconnected\n", client->id()); break;
        case WS_EVT_DATA: handleWebSocketMessage(arg, data, len); break;
        case WS_EVT_PONG: case WS_EVT_ERROR: break;
    }
}

void setup() {
  Serial.begin(115200);
  pinMode(SSR_TRIGGER_PIN, OUTPUT);
  digitalWrite(SSR_TRIGGER_PIN, LOW);
  pinMode(FOOTSWITCH_PIN, INPUT_PULLUP);

  long total_current = 0, total_voltage = 0;
  for (int i = 0; i < 500; i++) { 
    total_current += analogRead(ACS712_PIN); 
    total_voltage += analogRead(ZMPT_PIN);
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
}

void loop() {
  if (digitalRead(FOOTSWITCH_PIN) == LOW && !isWelding) {
    performWeld();
  }
  if (webTriggerActive && !isWelding){
    performWeld();
  }
  ws.cleanupClients();
}

void performWeld() {
    if(isWelding) return;
    isWelding = true;
    ws.textAll("{\"action\":\"status_update\", \"status\":\"Mengelas...\"}");
    
    float last_peak_current = 0.0;
    long last_weld_duration = 0;
    float last_energy_delivered = 0;
    float average_voltage = read_AC_RMS_Voltage();
    String finalStatus = "SIAP";

    digitalWrite(SSR_TRIGGER_PIN, HIGH);
    unsigned long startTime = micros();

    while (digitalRead(FOOTSWITCH_PIN) == LOW || webTriggerActive) { 
        float current_now = read_AC_RMS_Current();
        if (current_now > last_peak_current) last_peak_current = current_now;

        if (last_peak_current > MAX_PRIMARY_CURRENT) {
            finalStatus = "OVERCURRENT!";
            break;
        }

        unsigned long elapsedTime = (micros() - startTime) / 1000;
        
        if (currentMode == TIME_MODE && elapsedTime >= timePulse) break;
        if (currentMode == DUAL_PULSE_MODE && elapsedTime >= (dual_pulse1 + dual_delay + dual_pulse2)) break;
        
        if (currentMode == ENERGY_MODE) {
            unsigned long deltaTimeMicros = micros() - (startTime + last_weld_duration * 1000);
            last_energy_delivered += average_voltage * current_now * (deltaTimeMicros / 1000000.0);
            if (last_energy_delivered >= energyTarget) break;
        }

        if (currentMode == DUAL_PULSE_MODE) {
            if (elapsedTime > dual_pulse1 && elapsedTime < (dual_pulse1 + dual_delay)) {
                digitalWrite(SSR_TRIGGER_PIN, LOW);
            } else {
                digitalWrite(SSR_TRIGGER_PIN, HIGH);
            }
        }
        last_weld_duration = elapsedTime;
    }
    digitalWrite(SSR_TRIGGER_PIN, LOW);

    String jsonResult = "{\"action\":\"weld_result\"";
    jsonResult += ", \"voltage\":" + String(average_voltage);
    jsonResult += ", \"duration\":" + String(last_weld_duration);
    jsonResult += ", \"peak_current\":" + String(last_peak_current);
    jsonResult += ", \"energy\":" + String(last_energy_delivered);
    jsonResult += ", \"status\":\"" + finalStatus + "\"}";
    ws.textAll(jsonResult);
    
    isWelding = false;
}
