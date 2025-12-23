#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "UUID.h"
#include "FS.h"
#include "SPIFFS.h"
#include "time.h"

struct LogEntry {
  long long timestamp;
  double id;
};

//---- WiFi settings
const char* ssid = "Sousa";
const char* password = "olaola123";
//---- MQTT Broker settings
const char* mqtt_server = "5f479f2b79404567899d28e682008d15.s1.eu.hivemq.cloud";
const char* mqtt_username = "ESP32";
const char* mqtt_password = "Esp32isep";

const int mqtt_port = 8883;

//---- Log Size
const int LOG_SIZE = 100;

const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

//------ Topics
const char* loginAttemptEsp = "/coffee/esp32/auth/login_attempt";
const char* loginSuccessEsp = "/coffee/esp32/auth/login_success";
const char* coffeeDispensingEsp = "/coffee/esp32/dispense/requested";

//------ IDs
const char* id = "esp32";


//------ Flags

bool isLoggedIn = false;

int logIndex = 0;
int readIndex = 0;
int reconnectLoop = 5;

const int authButtonPin = 25;
const int coffeeButtonPin = 32;

int authButtonState = 0;
int coffeeButtonState = 0;

UUID uuid;

WiFiClientSecure espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
char data[100];
int value = 0;
bool wifiConnection = true;
bool wifiReset = false;

void setup() {
  delay(5000);
  Serial.begin(9600);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  Serial.println("\n");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  wifiConnection = true;
  randomSeed(micros());
  Serial.println("WiFi connected\nIP address: ");
  Serial.println(WiFi.localIP());
  while (!Serial) delay(1);
  uuid.seed(2);
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
  pinMode(authButtonPin, INPUT_PULLUP);
  pinMode(coffeeButtonPin, INPUT_PULLUP);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnection = false;
    reconnectWiFi();
  } else {
      if (!wifiConnection) {
        Serial.print("[WiFi] Reconnected!\n");
        wifiConnection = true;
      }
      if (reconnectLoop > 0)
        reconnectLoop--;
      if (!client.connected() && reconnectLoop == 0) {
        reconnectLoop = 5;
        reconnect();
      }
      client.loop();
      value++;
    }
  if (digitalRead(authButtonPin) == HIGH) {
    char timestamp[32];
    isLoggedIn = true;
    // turn LED on:
    Serial.println("NFC Login");
    StaticJsonDocument<128> doc;

    uuid.generate();

    getLocTime().toCharArray(timestamp, sizeof(timestamp));
    doc["timestamp"] = timestamp;
    doc["id"] = id;
    doc["uuid"] = uuid;
    doc["severity"] = "INFO";
    doc["source"] = id;
    doc["eventType"] = "authentication";
    doc["userId"] = 1;
    uint8_t jsonBuffer[1024];
    serializeJson(doc, (char*)jsonBuffer, sizeof(jsonBuffer));
    publishMessage(loginAttemptEsp, (char*)jsonBuffer, true);
    publishMessage(loginSuccessEsp, (char*)jsonBuffer, true);
    while (isLoggedIn) {
      Serial.println("Choose coffee");
      if (digitalRead(coffeeButtonPin) == HIGH) {
        doc["timestamp"] = timestamp;
        doc["id"] = id;
        doc["uuid"] = uuid;
        doc["severity"] = "INFO";
        doc["source"] = id;
        doc["eventType"] = "coffeeDispensing";
        doc["flavor"] = "BlackCoffeeNespresso";
        serializeJson(doc, (char*)jsonBuffer, sizeof(jsonBuffer));
        publishMessage(coffeeDispensingEsp, (char*)jsonBuffer, true);
        Serial.println("Coffee chosen");
        isLoggedIn = false;
      }
      delay(100);
    }
    client.loop();
  } else {
    Serial.println("Waiting for Login");
  }
  client.loop();
  delay(100);
}

unsigned long lastWifiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 2000;  // 10s

void reconnectWiFi() {
  unsigned long now = millis();
  if (now - lastWifiAttempt < WIFI_RETRY_INTERVAL) {
    return;
  }
  lastWifiAttempt = now;

  Serial.println("[WiFi] Reconnecting...");
  WiFi.disconnect();
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
}


void reconnect() {
  Serial.println("Attempting MQTT connection…");
  String clientId = "device-esp32";
  clientId += String(random(0xffff), HEX);
  if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
    Serial.println("connected");
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
  }
}

String getLocTime() {
  struct timeval now;
  gettimeofday(&now, NULL);
  if (now.tv_sec < 100000) {
    return "";
  }
  struct tm timeinfo;
  localtime_r(&now.tv_sec, &timeinfo);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

//=======================================
// This void is called every time we have a message from the broker
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Callback - ");
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

//======================================= publising as string
void publishMessage(const char* topic, String payload, boolean retained) {
  if (client.publish(topic, payload.c_str(), true))
    Serial.println("Message published [" + String(topic) + "]: " + payload);
}
