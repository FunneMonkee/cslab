#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "UUID.h"
#include "time.h"

const char* ssid = "Sousa";
const char* password = "olaola123";

const char* mqtt_server   = "5f479f2b79404567899d28e682008d15.s1.eu.hivemq.cloud";
const char* mqtt_username = "ESP32";
const char* mqtt_password = "Esp32isep";
const int   mqtt_port     = 8883;

const char* loginAttemptEsp     = "/coffee/esp32/auth/login_attempt";
const char* loginSuccessEsp     = "/coffee/esp32/auth/login_success";
const char* coffeeDispensingEsp = "/coffee/esp32/dispense/requested";

const char* deviceId = "esp32";

const int authButtonPin   = 25;
const int coffeeButtonPin = 32;

const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

const uint32_t DEBOUNCE_MS = 200;

WiFiClientSecure espClient;
PubSubClient client(espClient);
UUID uuid;

bool isLoggedIn = false;

SemaphoreHandle_t authButtonSem;
SemaphoreHandle_t coffeeButtonSem;

volatile uint32_t lastAuthISR = 0;
volatile uint32_t lastCoffeeISR = 0;

String getLocTime() {
  struct timeval now;
  gettimeofday(&now, NULL);
  if (now.tv_sec < 100000) return "";
  struct tm timeinfo;
  localtime_r(&now.tv_sec, &timeinfo);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

void publishMessage(const char* topic, const char* payload) {
  if (client.connected()) {
    client.publish(topic, payload, true);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

//TODO: debounce for buttons, might work for the nfc reader
void IRAM_ATTR authButtonISR() {
  uint32_t now = millis();
  if (now - lastAuthISR > DEBOUNCE_MS) {
    lastAuthISR = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(authButtonSem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}

void IRAM_ATTR coffeeButtonISR() {
  uint32_t now = millis();
  if (now - lastCoffeeISR > DEBOUNCE_MS) {
    lastCoffeeISR = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(coffeeButtonSem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}

void wifiMqttTask(void* pvParameters) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, password);
      while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
    }

    if (!client.connected()) {
      String clientId = "esp32-" + String(random(0xffff), HEX);
      client.connect(clientId.c_str(), mqtt_username, mqtt_password);
    }

    client.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void authTask(void* pvParameters) {
  for (;;) {
    if (xSemaphoreTake(authButtonSem, portMAX_DELAY)) {
      if (isLoggedIn) continue;

      StaticJsonDocument<256> doc;
      uuid.generate();

      doc["timestamp"] = getLocTime();
      doc["id"] = deviceId;
      doc["uuid"] = uuid.toCharArray();
      doc["severity"] = "INFO";
      doc["eventType"] = "authentication";
      doc["userId"] = 1;

      char buffer[256];
      serializeJson(doc, buffer);

      publishMessage(loginAttemptEsp, buffer);
      publishMessage(loginSuccessEsp, buffer);

      isLoggedIn = true;
    }
  }
}

void coffeeTask(void* pvParameters) {
  for (;;) {
    if (xSemaphoreTake(coffeeButtonSem, portMAX_DELAY)) {
      if (!isLoggedIn) continue;

      StaticJsonDocument<256> doc;

      doc["timestamp"] = getLocTime();
      doc["id"] = deviceId;
      doc["severity"] = "INFO";
      doc["eventType"] = "coffeeDispensing";
      doc["flavor"] = "BlackCoffeeNespresso";

      char buffer[256];
      serializeJson(doc, buffer);

      publishMessage(coffeeDispensingEsp, buffer);

      isLoggedIn = false;
    }
  }
}

void setup() {
  Serial.begin(9600);
  delay(2000);

  pinMode(authButtonPin, INPUT_PULLUP);
  pinMode(coffeeButtonPin, INPUT_PULLUP);

  authButtonSem = xSemaphoreCreateBinary();
  coffeeButtonSem = xSemaphoreCreateBinary();

  attachInterrupt(authButtonPin, authButtonISR, RISING);
  attachInterrupt(coffeeButtonPin, coffeeButtonISR, RISING);

  WiFi.mode(WIFI_STA);
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
  uuid.seed(2);

  //TODO: decide on stack size
  xTaskCreatePinnedToCore(wifiMqttTask, "WiFiMQTT", 4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(authTask, "AuthTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(coffeeTask, "CoffeeTask", 4096, NULL, 2, NULL, 1);
}

void loop() {
  //TODO: not sure if needed, might just work with an empty loop()
  vTaskDelay(portMAX_DELAY);
}
