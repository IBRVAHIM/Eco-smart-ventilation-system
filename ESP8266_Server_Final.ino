#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <vector>


// ================= MODES =================
enum SystemMode { ECO, BALANCED, COMFORT };
SystemMode currentMode = BALANCED;


// ===== Air Conditioner Limits =====
float acTempLimit = 37.0;
float acHumLimit  = 95.0;
int   acCo2Limit  = 1000;

// ===== Fan Limits =====
float fanTempLimit = 32.0;
float fanHumLimit  = 75.0;
int   fanCo2Limit  = 750;


// ================= CUSTOMIZABLE SYSTEM =================
bool fanDeviceEnabled = true;
bool acDeviceEnabled = true;
bool stepperDeviceEnabled = true;


// Forward declaration
void broadcastLimits();


// ================= APPLY MODE =================
void applyMode(SystemMode mode){

  if(mode == ECO){

    fanTempLimit = 28;
    fanHumLimit  = 65;
    fanCo2Limit  = 700;

    acTempLimit  = 32;
    acHumLimit   = 80;
    acCo2Limit   = 900;
  }

  if(mode == BALANCED){

    fanTempLimit = 25;
    fanHumLimit  = 55;
    fanCo2Limit  = 750;

    acTempLimit  = 28;
    acHumLimit   = 70;
    acCo2Limit   = 1000;
  }

  if(mode == COMFORT){

    fanTempLimit = 23;
    fanHumLimit  = 50;
    fanCo2Limit  = 800;

    acTempLimit  = 24;
    acHumLimit   = 65;
    acCo2Limit   = 1100;
  }

  currentMode = mode;

  Serial.println("MODE UPDATED");
  
  // Broadcast updated limits to dashboard
  broadcastLimits();
}


// =====================================================

IPAddress staticIP(192,168,1,100),
          gateway(192,168,1,1),
          subnet(255,255,255,0);

const char* ssid = "Home";
const char* password = "Home123@";

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);


struct SensorData {
  String id;
  float temperature;
  float humidity;
  int co2;
  unsigned long lastSeen;
};

std::vector<SensorData> sensorNodes;
StaticJsonDocument<256> jsonDoc;

bool lastAlertAC = false;
bool lastAlertFan = false;


// ========== Broadcast Limits ==========
void broadcastLimits() {
  StaticJsonDocument<256> limitsDoc;
  
  limitsDoc["type"] = "limits";
  
  JsonObject fan = limitsDoc.createNestedObject("fan");
  fan["temp"] = fanTempLimit;
  fan["hum"] = fanHumLimit;
  fan["co2"] = fanCo2Limit;
  
  JsonObject ac = limitsDoc.createNestedObject("ac");
  ac["temp"] = acTempLimit;
  ac["hum"] = acHumLimit;
  ac["co2"] = acCo2Limit;
  
  String output;
  serializeJson(limitsDoc, output);
  webSocket.broadcastTXT(output);
}


// ========== Limit Check ==========
void checkSensorLimits(float t, float h, int c) {

  bool acAlert = false;
  bool fanAlert = false;

  // Check only if device is enabled
  if (acDeviceEnabled && (t > acTempLimit || h > acHumLimit || c > acCo2Limit)) 
    acAlert = true;
    
  if (fanDeviceEnabled && (t > fanTempLimit || h > fanHumLimit || c > fanCo2Limit)) 
    fanAlert = true;

  // Send commands only if device is enabled
  if (acAlert && !lastAlertAC && acDeviceEnabled) {
    webSocket.broadcastTXT("SET_AC");
  }
  else if (!acAlert && lastAlertAC) {
    webSocket.broadcastTXT("RESET_AC");
  }

  lastAlertAC = acAlert;


  if (fanAlert && !lastAlertFan && fanDeviceEnabled) {
    webSocket.broadcastTXT("SET_FAN");
  }
  else if (!fanAlert && lastAlertFan) {
    webSocket.broadcastTXT("RESET_FAN");
  }

  lastAlertFan = fanAlert;
}


// ========== Sensor Update ==========
void updateOrAddSensor(const String& id, float t, float h, int c) {

  for (auto &node : sensorNodes) {

    if (node.id == id) {

      node.temperature = t;
      node.humidity = h;
      node.co2 = c;
      node.lastSeen = millis();

      checkSensorLimits(t, h, c);
      return;
    }
  }

  sensorNodes.push_back({id, t, h, c, millis()});

  checkSensorLimits(t, h, c);
}


// ========== Broadcast Dashboard ==========
void broadcastDashboard() {

  StaticJsonDocument<1024> outDoc;

  outDoc["type"] = "sensors";

  JsonArray arr = outDoc.createNestedArray("nodes");

  for (auto &n : sensorNodes) {

    JsonObject obj = arr.createNestedObject();

    obj["id"] = n.id;
    obj["temperature"] = n.temperature;
    obj["humidity"] = n.humidity;
    obj["co2"] = n.co2;
    obj["lastSeen"] = (millis() - n.lastSeen)/1000;
  }

  String out;
  serializeJson(outDoc, out);

  webSocket.broadcastTXT(out);
}



// ========== WebSocket ==========
void onWsEvent(uint8_t num, WStype_t type,
               uint8_t * payload, size_t length) {

  switch (type) {

    case WStype_CONNECTED: {

      webSocket.sendTXT(num,"REQ_DATA");
      
      // Send current limits to newly connected client
      delay(100);
      broadcastLimits();
    }
    break;


    case WStype_TEXT: {

      String msg = String((char*)payload);

      Serial.println(msg);


      // ================= CUSTOMIZABLE SYSTEM =================
      
      if(msg.startsWith("DEVICE_FAN:")){
        fanDeviceEnabled = (msg.substring(11) == "1");
        Serial.printf("Fan Device: %s\n", fanDeviceEnabled ? "ENABLED" : "DISABLED");
        return;
      }

      if(msg.startsWith("DEVICE_AC:")){
        acDeviceEnabled = (msg.substring(10) == "1");
        Serial.printf("AC Device: %s\n", acDeviceEnabled ? "ENABLED" : "DISABLED");
        return;
      }

      if(msg.startsWith("DEVICE_STEPPER:")){
        stepperDeviceEnabled = (msg.substring(15) == "1");
        Serial.printf("Stepper Device: %s\n", stepperDeviceEnabled ? "ENABLED" : "DISABLED");
        return;
      }


      // ================= MODES =================

      if(msg == "MODE_ECO"){
        applyMode(ECO);
        return;
      }

      if(msg == "MODE_BAL"){
        applyMode(BALANCED);
        return;
      }

      if(msg == "MODE_COM"){
        applyMode(COMFORT);
        return;
      }

      // =========================================


      // ---- Old Mode toggle ----
      if (msg.startsWith("MODE:")) {
        webSocket.broadcastTXT(msg);
        return;
      }


      // ---- Stepper ----
      if (msg.startsWith("STEPPER:")) {
        // Only forward if stepper is enabled
        if(stepperDeviceEnabled){
          webSocket.broadcastTXT(msg);
        }
        return;
      }


      // ---- JSON ----
      DeserializationError err = deserializeJson(jsonDoc, msg);

      if (err) return;


      if (jsonDoc.containsKey("id")) {

        String id = jsonDoc["id"];
        float t = jsonDoc["temperature"];
        float h = jsonDoc["humidity"];
        int c   = jsonDoc["co2"];

        updateOrAddSensor(id, t, h, c);

        broadcastDashboard();

        return;
      }

      // Manual limits update from dashboard
      if (jsonDoc.containsKey("limits")) {

        if (jsonDoc["limits"].containsKey("fan")) {
          fanTempLimit = jsonDoc["limits"]["fan"]["temperature"];
          fanHumLimit  = jsonDoc["limits"]["fan"]["humidity"];
          fanCo2Limit  = jsonDoc["limits"]["fan"]["co2"];
        }

        if (jsonDoc["limits"].containsKey("ac")) {
          acTempLimit = jsonDoc["limits"]["ac"]["temperature"];
          acHumLimit  = jsonDoc["limits"]["ac"]["humidity"];
          acCo2Limit  = jsonDoc["limits"]["ac"]["co2"];
        }

        Serial.println("Manual limits updated");

        // Re-check all sensors with new limits
        for (auto &n : sensorNodes) {
          checkSensorLimits(n.temperature, n.humidity, n.co2);
        }

        broadcastLimits();
        return;
      }
    }
    break;


    case WStype_DISCONNECTED:

      Serial.println("Client Disconnected");
      break;
  }
}



// ========== Dashboard ==========
String dashboardHTML() {

String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Smart Home Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0">

<style>
*{margin:0;padding:0;box-sizing:border-box}
body{
  font-family:'Segoe UI',Arial,sans-serif;
  background: linear-gradient(135deg, #0f2027, #203a43, #2c5364);
  min-height:100vh;
  padding:24px;
  font-size:16px;
  color:#fff;
}
.container{max-width:1400px;margin:0 auto}
h2{
  text-align:center;
  font-size:32px;
  font-weight:700;
  margin-bottom:28px;
  letter-spacing:1px;
  text-shadow:0 2px 8px rgba(0,0,0,0.4);
}
h3{
  font-size:18px;
  font-weight:600;
  margin-bottom:14px;
  padding-bottom:8px;
  border-bottom:2px solid rgba(255,255,255,0.2);
  color:#e0e0e0;
}
.card{
  background:rgba(255,255,255,0.07);
  backdrop-filter:blur(12px);
  border:1px solid rgba(255,255,255,0.12);
  padding:22px;
  margin-bottom:20px;
  border-radius:16px;
  box-shadow:0 8px 32px rgba(0,0,0,0.3);
}
button{
  padding:11px 22px;
  margin:5px;
  font-size:15px;
  font-weight:600;
  border:none;
  border-radius:8px;
  cursor:pointer;
  background:linear-gradient(135deg,#007bff,#0056b3);
  color:white;
  transition:all 0.3s;
  box-shadow:0 4px 12px rgba(0,123,255,0.3);
}
button:hover{
  transform:translateY(-2px);
  box-shadow:0 6px 16px rgba(0,123,255,0.5);
}
button:active{transform:translateY(0)}
button.active{
  background:linear-gradient(135deg,#28a745,#1e7e34);
  box-shadow:0 4px 12px rgba(40,167,69,0.4);
}
table{width:100%;border-collapse:collapse;margin-top:10px}
th,td{padding:14px;text-align:center;font-size:15px;border-bottom:1px solid rgba(255,255,255,0.08)}
th{background:rgba(0,123,255,0.3);color:#fff;font-weight:600;letter-spacing:0.5px}
tr:hover td{background:rgba(255,255,255,0.05)}
.device-control label{
  margin-right:25px;
  font-size:16px;
  cursor:pointer;
  user-select:none;
  color:#ddd;
}
.device-control input[type="checkbox"]{
  width:18px;height:18px;
  cursor:pointer;
  margin-right:8px;
  vertical-align:middle;
  accent-color:#007bff;
}
.limits-grid{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin-top:15px}
.limit-box{
  background:rgba(255,255,255,0.05);
  padding:16px;
  border-radius:10px;
  border-left:4px solid #007bff;
}
.limit-box h4{margin-bottom:12px;color:#7bc8ff;font-size:17px}
.limit-item{margin:8px 0;font-size:15px;color:#ccc}
.limit-item strong{color:#fff;min-width:110px;display:inline-block}
.limit-input{
  background:rgba(255,255,255,0.1);
  border:1px solid rgba(255,255,255,0.2);
  color:#fff;
  padding:6px 10px;
  border-radius:6px;
  width:90px;
  font-size:14px;
}
.limit-input:focus{outline:none;border-color:#007bff}
.mode-buttons{display:flex;gap:10px;flex-wrap:wrap}
.slider-container{margin-top:16px}
input[type="range"]{
  width:100%;height:8px;
  border-radius:5px;
  background:rgba(255,255,255,0.2);
  outline:none;
  -webkit-appearance:none;
  margin-top:8px;
}
input[type="range"]::-webkit-slider-thumb{
  -webkit-appearance:none;
  width:22px;height:22px;
  border-radius:50%;
  background:linear-gradient(135deg,#007bff,#00d4ff);
  cursor:pointer;
  box-shadow:0 2px 8px rgba(0,123,255,0.5);
}
.angle-display{
  font-size:22px;
  font-weight:700;
  color:#00d4ff;
  margin-left:12px;
}
.limits-toggle{
  display:flex;
  gap:10px;
  margin-bottom:16px;
}
.toggle-btn{
  padding:8px 18px;
  font-size:14px;
  border-radius:20px;
  background:rgba(255,255,255,0.1);
  border:1px solid rgba(255,255,255,0.2);
  color:#ccc;
  cursor:pointer;
  transition:all 0.3s;
}
.toggle-btn.active{
  background:linear-gradient(135deg,#007bff,#0056b3);
  color:#fff;
  border-color:#007bff;
  box-shadow:0 4px 12px rgba(0,123,255,0.4);
}
.manual-limits{display:none}
.manual-limits.show{display:block}
.save-btn{
  background:linear-gradient(135deg,#28a745,#1e7e34);
  margin-top:10px;
  box-shadow:0 4px 12px rgba(40,167,69,0.3);
}
@media (max-width: 768px){
  .limits-grid{grid-template-columns:1fr}
  .mode-buttons{flex-direction:column}
  button{width:100%}
}
</style>
</head>

<body>
<div class="container">

<h2>🏠 Smart Home Control Panel</h2>


<!-- DEVICES -->
<div class="card">
<h3>⚙️ Active Devices</h3>
<div class="device-control">
<label><input type="checkbox" id="fanEnabled" checked> 🌀 Fan</label>
<label><input type="checkbox" id="acEnabled" checked> ❄️ AC</label>
<label><input type="checkbox" id="stepperEnabled" checked> 🪟 Window (Stepper)</label>
</div>
</div>


<!-- SYSTEM MODE -->
<div class="card">
<h3>🎛️ System Mode</h3>
<div class="mode-buttons">
<button id="btnEco" onclick="setMode('ECO')">🌿 Eco</button>
<button id="btnBal" onclick="setMode('BAL')" class="active">⚖️ Balanced</button>
<button id="btnCom" onclick="setMode('COM')">⭐ Comfort</button>
</div>
</div>


<!-- LIMITS -->
<div class="card">
<h3>📊 Limits Control</h3>

<!-- Toggle -->
<div class="limits-toggle">
<button class="toggle-btn active" id="toggleMode" onclick="switchLimitsMode('mode')">🎛️ Mode Limits</button>
<button class="toggle-btn" id="toggleManual" onclick="switchLimitsMode('manual')">✏️ Manual Limits</button>
</div>

<!-- Mode Limits Display -->
<div id="modeLimits">
<div class="limits-grid">
<div class="limit-box">
<h4>🌀 Fan Limits</h4>
<div class="limit-item"><strong>Temperature:</strong> <span id="fanTempLimit">--</span> °C</div>
<div class="limit-item"><strong>Humidity:</strong> <span id="fanHumLimit">--</span> %</div>
<div class="limit-item"><strong>CO₂:</strong> <span id="fanCo2Limit">--</span> ppm</div>
</div>
<div class="limit-box">
<h4>❄️ AC Limits</h4>
<div class="limit-item"><strong>Temperature:</strong> <span id="acTempLimit">--</span> °C</div>
<div class="limit-item"><strong>Humidity:</strong> <span id="acHumLimit">--</span> %</div>
<div class="limit-item"><strong>CO₂:</strong> <span id="acCo2Limit">--</span> ppm</div>
</div>
</div>
</div>

<!-- Manual Limits Inputs -->
<div id="manualLimits" class="manual-limits">
<div class="limits-grid">
<div class="limit-box">
<h4>🌀 Fan Limits</h4>
<div class="limit-item"><strong>Temperature:</strong> <input class="limit-input" type="number" id="mFanTemp" step="0.5"> °C</div>
<div class="limit-item"><strong>Humidity:</strong> <input class="limit-input" type="number" id="mFanHum" step="1"> %</div>
<div class="limit-item"><strong>CO₂:</strong> <input class="limit-input" type="number" id="mFanCo2"> ppm</div>
</div>
<div class="limit-box">
<h4>❄️ AC Limits</h4>
<div class="limit-item"><strong>Temperature:</strong> <input class="limit-input" type="number" id="mAcTemp" step="0.5"> °C</div>
<div class="limit-item"><strong>Humidity:</strong> <input class="limit-input" type="number" id="mAcHum" step="1"> %</div>
<div class="limit-item"><strong>CO₂:</strong> <input class="limit-input" type="number" id="mAcCo2"> ppm</div>
</div>
</div>
<button class="save-btn" onclick="saveManualLimits()">💾 Save Limits</button>
</div>
</div>


<!-- MANUAL CONTROL -->
<div class="card">
<h3>🎮 Manual Window Control</h3>
<button id="modeBtn">🤖 Switch to MANUAL</button>
<div class="slider-container">
<label style="color:#ccc"><strong>Window Angle:</strong></label>
<input type="range" id="stepperSlider" min="0" max="180" value="0">
<span class="angle-display" id="angleVal">0°</span>
</div>
</div>


<!-- SENSOR TABLE -->
<div class="card">
<h3>📡 Live Sensor Data</h3>
<table>
<thead>
<tr>
<th>Node ID</th>
<th>🌡️ Temp (°C)</th>
<th>💧 Humidity (%)</th>
<th>🌫️ CO₂ (ppm)</th>
<th>⏱️ Last Seen (sec)</th>
</tr>
</thead>
<tbody id="body"></tbody>
</table>
</div>

</div>


<script>
let socket = new WebSocket("ws://"+location.hostname+":81");
let manualMode = false;
let limitsMode = "mode"; // "mode" or "manual"


// ===== Devices =====
document.getElementById('fanEnabled').onchange = function(){
  socket.send("DEVICE_FAN:" + (this.checked ? "1" : "0"));
};
document.getElementById('acEnabled').onchange = function(){
  socket.send("DEVICE_AC:" + (this.checked ? "1" : "0"));
};
document.getElementById('stepperEnabled').onchange = function(){
  socket.send("DEVICE_STEPPER:" + (this.checked ? "1" : "0"));
};


// ===== Limits Toggle =====
function switchLimitsMode(mode){
  limitsMode = mode;
  document.getElementById("modeLimits").style.display = (mode === "mode") ? "block" : "none";
  document.getElementById("manualLimits").style.display = (mode === "manual") ? "block" : "none";
  document.getElementById("toggleMode").classList.toggle("active", mode === "mode");
  document.getElementById("toggleManual").classList.toggle("active", mode === "manual");
}

function saveManualLimits(){
  let limits = {
    limits: {
      fan: {
        temperature: parseFloat(document.getElementById("mFanTemp").value),
        humidity: parseFloat(document.getElementById("mFanHum").value),
        co2: parseInt(document.getElementById("mFanCo2").value)
      },
      ac: {
        temperature: parseFloat(document.getElementById("mAcTemp").value),
        humidity: parseFloat(document.getElementById("mAcHum").value),
        co2: parseInt(document.getElementById("mAcCo2").value)
      }
    }
  };
  socket.send(JSON.stringify(limits));
}


// ===== System Mode =====
function setMode(m){
  socket.send("MODE_" + m);
  document.getElementById("btnEco").classList.remove("active");
  document.getElementById("btnBal").classList.remove("active");
  document.getElementById("btnCom").classList.remove("active");
  if(m === "ECO") document.getElementById("btnEco").classList.add("active");
  if(m === "BAL") document.getElementById("btnBal").classList.add("active");
  if(m === "COM") document.getElementById("btnCom").classList.add("active");
}


// ===== Manual/Auto =====
const modeBtn = document.getElementById("modeBtn");
modeBtn.onclick = function(){
  manualMode = !manualMode;
  if (manualMode) {
    socket.send("MODE:MANUAL");
    this.innerText = "🤖 Switch to AUTO";
    this.classList.add("active");
  } else {
    socket.send("MODE:AUTO");
    this.innerText = "🤖 Switch to MANUAL";
    this.classList.remove("active");
  }
};


// ===== Stepper Slider =====
const slider = document.getElementById("stepperSlider");
const angleVal = document.getElementById("angleVal");
slider.oninput = function() {
  angleVal.innerText = this.value + "°";
  if (manualMode) {
    socket.send("STEPPER:" + this.value);
  }
};


// ===== Incoming Messages =====
socket.onmessage = function(event) {
  try {
    let json = JSON.parse(event.data);

    // Sensor data
    if (json.type === "sensors" && json.nodes) {
      let body = document.getElementById("body");
      body.innerHTML = "";
      json.nodes.forEach(node => {
        body.innerHTML += `
        <tr>
        <td>${node.id}</td>
        <td>${node.temperature.toFixed(1)}</td>
        <td>${node.humidity.toFixed(1)}</td>
        <td>${node.co2}</td>
        <td>${node.lastSeen}</td>
        </tr>`;
      });
    }

    // Limits update
    if (json.type === "limits") {
      document.getElementById("fanTempLimit").innerText = json.fan.temp.toFixed(1);
      document.getElementById("fanHumLimit").innerText = json.fan.hum.toFixed(1);
      document.getElementById("fanCo2Limit").innerText = json.fan.co2;
      document.getElementById("acTempLimit").innerText = json.ac.temp.toFixed(1);
      document.getElementById("acHumLimit").innerText = json.ac.hum.toFixed(1);
      document.getElementById("acCo2Limit").innerText = json.ac.co2;

      // Fill manual inputs with current values
      document.getElementById("mFanTemp").value = json.fan.temp;
      document.getElementById("mFanHum").value = json.fan.hum;
      document.getElementById("mFanCo2").value = json.fan.co2;
      document.getElementById("mAcTemp").value = json.ac.temp;
      document.getElementById("mAcHum").value = json.ac.hum;
      document.getElementById("mAcCo2").value = json.ac.co2;
    }

  } catch(e){
    console.log("Non-JSON:", event.data);
  }
};
</script>

</body>
</html>
)rawliteral";

return page;
}



// ========== HTTP ==========
void handleRoot() {

  server.send(200,"text/html",dashboardHTML());
}



// ========== Setup ==========
void setup() {

  Serial.begin(115200);

  pinMode(2, OUTPUT);

  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  Serial.println("Connected");
  Serial.println(WiFi.localIP());


  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWsEvent);


  // Default Mode
  applyMode(BALANCED);
}



// ========== Loop ==========
void loop() {

  server.handleClient();
  webSocket.loop();
}
