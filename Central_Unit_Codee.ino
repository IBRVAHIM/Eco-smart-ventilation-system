#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Ticker.h>
#include <EEPROM.h>

#define EEPROM_SIZE 8

const char* ssid     = "Home";
const char* password = "Home123@";

const char* serverAddress = "192.168.1.100"; 
const int serverPort = 81;

WebSocketsClient webSocket;
Ticker myTimer;

// ================= Pins =================
#define STEP_PIN 18
#define DIR_PIN 19
#define EN_PIN 21

#define LDR_LEFT 34
#define LDR_RIGHT 35

#define BTN_RUN 32
#define BTN_DIR 33

#define FANpin 22
#define ACpin 23

// ================= Stepper =================
const int FULL_STEPS_PER_REV = 200;
const int MICROSTEPS = 1;
const int STEPS_PER_REV = FULL_STEPS_PER_REV * MICROSTEPS;

int currentStep = 0;
const float MIN_ANGLE_CHANGE = 10.0;
float CurrentAngle = 0;

// ================= Flags =================
bool FlagRunFunction = 0;
bool FlagGetFan = 0;
bool TimerFlag = 0;
bool isManualMode = false;
bool motorRunning = false;

// =================================================
// Save motor position in EEPROM (only when motor stops)

void savePosition() {
  EEPROM.put(0, currentStep);
  EEPROM.commit();
}

// =================================================

void stepMotor(int steps) {

  digitalWrite(EN_PIN, LOW);  // Enable motor driver

  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(900);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(900);
    yield();
  }

  digitalWrite(EN_PIN, HIGH);  // Disable driver after movement
}

// =================================================

void moveToPosition(int targetStep) {

  int maxSteps = STEPS_PER_REV / 2;

  // Limit movement between 0° and 180°
  if (targetStep < 0) targetStep = 0;
  if (targetStep > maxSteps) targetStep = maxSteps;

  int diff = targetStep - currentStep;
  if (diff == 0) return;

  digitalWrite(DIR_PIN, diff > 0 ? LOW : HIGH);

  stepMotor(abs(diff));

  currentStep = targetStep;

  // Save position after finishing movement
  savePosition();
}

// =================================================

void setStepperAngle(float targetAngle) {

  // Keep angle inside valid range
  if (targetAngle < 0) targetAngle = 0;
  if (targetAngle > 180) targetAngle = 180;

  int maxSteps = STEPS_PER_REV / 2;
  int targetStep = targetAngle * maxSteps / 180.0;

  float cur = currentStep * 180.0 / maxSteps;
  float diff = targetAngle - cur;

  Serial.printf("Request: %.1f | Curr: %.1f\n", targetAngle, cur);

  // Move only if the change is large enough
  if (fabs(diff) >= MIN_ANGLE_CHANGE) {
    moveToPosition(targetStep);
  }

  CurrentAngle = targetAngle;
}

// =================================================

int readAverage(int pin) {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delayMicroseconds(200);
  }
  return sum / 10;
}

// =================================================
// LDR Tracking (Auto Mode)

void checkLDR() {

  if (!FlagRunFunction && !isManualMode) {

    if (TimerFlag) {

      TimerFlag = 0;

      int L = readAverage(LDR_LEFT);
      int R = readAverage(LDR_RIGHT);

      int diff = L - R;

      Serial.printf("LDR L:%d R:%d Diff:%d\n", L, R, diff);

      if (diff > 200) {
        setStepperAngle(45);
      }
      else if (diff < -200) {
        setStepperAngle(135);
      }
      else {
        setStepperAngle(90);
      }
    }
  }
}

// =================================================
// AC Control

void checkAC() {

  if (FlagRunFunction) {
    digitalWrite(ACpin, HIGH);
    digitalWrite(FANpin, LOW);

    // When AC is ON, rotate panel to 180°
    if (CurrentAngle != 180) {
      setStepperAngle(180);
    }
  }
  else {
    digitalWrite(ACpin, LOW);
  }
}

// =================================================
// Fan Control

void checkFan() {

  if (FlagGetFan && !FlagRunFunction) {
    digitalWrite(FANpin, HIGH);
  }
  else {
    digitalWrite(FANpin, LOW);
  }
}

// =================================================
// Manual Mode Buttons

void handleSwitches() {

  if (!isManualMode) return;

  static bool lastRun = HIGH;
  static bool lastHome = HIGH;

  bool curRun = digitalRead(BTN_RUN);
  bool curHome = digitalRead(BTN_DIR);

  // Button 1: Start / Stop motor
  if (lastRun == HIGH && curRun == LOW) {
    motorRunning = !motorRunning;
    Serial.printf("Motor: %s\n", motorRunning ? "RUNNING" : "STOPPED");

    // Save position if motor stops
    if (!motorRunning) {
      savePosition();
    }
  }

  // Button 2: Go back to 0°
  if (lastHome == HIGH && curHome == LOW) {
    motorRunning = false;
    setStepperAngle(0);
    Serial.println("Homed to 0°");
  }

  lastRun = curRun;
  lastHome = curHome;

  // Manual continuous movement
  if (motorRunning) {

    int maxSteps = STEPS_PER_REV / 2;

    if (currentStep < maxSteps) {
      digitalWrite(EN_PIN, LOW);
      digitalWrite(DIR_PIN, LOW);
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(2000);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(2000);
      currentStep++;
    }
    else {
      // Stop at 180° limit
      motorRunning = false;
      digitalWrite(EN_PIN, HIGH);
      savePosition();
      Serial.println("Limit 180° reached");
    }
  }
}

// =================================================

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

  if (type == WStype_CONNECTED) {
    Serial.println("[WS] Connected");
  }

  if (type == WStype_DISCONNECTED) {
    Serial.println("[WS] Disconnected");
  }

  if (type == WStype_TEXT) {

    String msg = "";
    for (int i = 0; i < length; i++)
      msg += (char)payload[i];

    Serial.println(msg);

    if (msg == "SET_FAN")    FlagGetFan = 1;
    if (msg == "RESET_FAN")  FlagGetFan = 0;
    if (msg == "SET_AC")     FlagRunFunction = 1;
    if (msg == "RESET_AC")   FlagRunFunction = 0;

    if (msg == "MODE:MANUAL") {
      isManualMode = true;
      Serial.println("Manual Mode");
    }

    if (msg == "MODE:AUTO") {
      isManualMode = false;
      Serial.println("Auto Mode");
    }

    // Manual angle control from WebSocket
    if (msg.startsWith("STEPPER:") && isManualMode) {
      int a = msg.substring(8).toInt();
      setStepperAngle(a);
    }
  }
}

// =================================================

void onTick() {
  TimerFlag = true;
}

// =================================================

void setup() {

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);

  pinMode(BTN_RUN, INPUT_PULLUP);
  pinMode(BTN_DIR, INPUT_PULLUP);

  pinMode(FANpin, OUTPUT);
  pinMode(ACpin, OUTPUT);

  digitalWrite(EN_PIN, HIGH);  // Driver disabled by default
  digitalWrite(FANpin, LOW);
  digitalWrite(ACpin, LOW);

  Serial.begin(115200);
  delay(2000);

  Serial.println("============================");
  Serial.println("ESP32 Smart Home System");
  Serial.println("============================");

  // Restore last saved motor position
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, currentStep);

  int maxSteps = STEPS_PER_REV / 2;
  if (currentStep < 0 || currentStep > maxSteps) {
    currentStep = 0;
  }

  CurrentAngle = currentStep * 180.0 / maxSteps;
  Serial.printf("Restored position: %.1f°\n", CurrentAngle);

  // Connect to WiFi
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  webSocket.begin(serverAddress, serverPort, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);

  myTimer.attach_ms(500, onTick);

  Serial.println("System Ready!");
}

// =================================================

void loop() {

  webSocket.loop();

  if (!isManualMode) {
    checkLDR();
    checkAC();
    checkFan();
  }
  else {
    checkAC();
    checkFan();
  }

  handleSwitches();
}
