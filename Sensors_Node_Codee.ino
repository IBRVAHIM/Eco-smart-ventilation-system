#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <DHT.h>
#include <Ticker.h>

// =============== WiFi Credentials ===============
const char* ssid = "Home";
const char* password = "Home123@";

// =============== Firebase Configuration ===============
#define FIREBASE_HOST "ecosmartventilationsystem-default-rtdb.europe-west1.firebasedatabase.app"
#define FIREBASE_AUTH "DvWf2bkr7hYqr7lr8xrqmhUcHvNm4gc8TAItSJ8m"  // Keep empty if using test mode

FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

// =============== Node Configuration ===============
String nodeID = "node_1";  // Sensor node ID

// =============== DHT22 Setup ===============
#define DHTPIN D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// =============== MQ Gas Sensor Setup ===============
#define MQ_PIN A0

// =============== Allowed Change (Tolerances) ===============
float tempTolerance = 0.5;
float humTolerance = 2.0;
int mqTolerance = 10;

// =============== Last Values Sent to Firebase ===============
float lastTemp = NAN;
float lastHum = NAN;
int lastMQ = -1;

// =============== Timer Setup ===============
Ticker timer;
volatile bool flag = false;

void everySecond() {
  flag = true;  // This runs every 1 second
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);

  // =============== Connect to WiFi ===============
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(2, !digitalRead(2));  // Blink LED while connecting
  }

  Serial.println("\nConnected to WiFi!");
  digitalWrite(2, HIGH);

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // =============== Firebase Initialization ===============
  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  firebaseData.setBSSLBufferSize(1024, 1024);

  Serial.println("Firebase initialized!");

  // =============== Start Timer (1 second interval) ===============
  timer.attach(1, everySecond);
}

void loop() {

  if (flag) {
    flag = false;

    // =============== Read Sensor Values ===============
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int mqValue = analogRead(MQ_PIN);

    if (isnan(t) || isnan(h)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // =============== Check if Data Needs to Be Sent ===============
    bool shouldSend = false;

    if (isnan(lastTemp) || fabs(t - lastTemp) >= tempTolerance)
      shouldSend = true;

    if (isnan(lastHum) || fabs(h - lastHum) >= humTolerance)
      shouldSend = true;

    if (lastMQ < 0 || abs(mqValue - lastMQ) >= mqTolerance)
      shouldSend = true;

    if (shouldSend) {

      // Base path for this sensor node inside Firebase
      String basePath = "/sensors/" + nodeID;

      Firebase.setFloat(firebaseData, basePath + "/temperature", t);
      Firebase.setFloat(firebaseData, basePath + "/humidity", h);
      Firebase.setInt(firebaseData, basePath + "/co2", mqValue);
      Firebase.setInt(firebaseData, basePath + "/lastSeen", millis() / 1000);

      Serial.printf(
        "Sent to Firebase: Temp=%.1f°C, Hum=%.1f%%, CO2=%d\n",
        t, h, mqValue
      );

      // Save last sent values to avoid unnecessary updates
      lastTemp = t;
      lastHum = h;
      lastMQ = mqValue;
    }
  }
}
