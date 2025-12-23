#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include "time.h"

const char* ssid = "Sousa";
const char* password = "olaola123";

const char* mqtt_server   = "5f479f2b79404567899d28e682008d15.s1.eu.hivemq.cloud";
const char* mqtt_username = "RaspberryPico";
const char* mqtt_password = "Raspberrypico2isep";
const int   mqtt_port     = 8883;

const char* loginSuccessEsp     = "/coffee/esp32/auth/login_success";
const char* coffeeDispensingEsp = "/coffee/esp32/dispense/requested";
//keep for now
const char* testTopic           = "/test";

const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

WiFiClientSecure raspClient;
PubSubClient client(raspClient);

Servo servo;

bool isLoggedIn = false;

SemaphoreHandle_t loginSem;
SemaphoreHandle_t coffeeSem;
SemaphoreHandle_t testSem;

void retrieveCoffeeCapsule() {
  servo.write(180);
  vTaskDelay(pdMS_TO_TICKS(250));
  servo.write(0);
  vTaskDelay(pdMS_TO_TICKS(250));
}

int diff_asm(int a, int b) {
  int result;
  asm volatile (
    "sub %0, %1, %2\n"
    : "=r"(result)
    : "r"(a), "r"(b)
  );
  return result;
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, loginSuccessEsp) == 0) {
    xSemaphoreGive(loginSem);
  } else if (strcmp(topic, coffeeDispensingEsp) == 0) {
    xSemaphoreGive(coffeeSem);
  } else if (strcmp(topic, testTopic) == 0) {
    xSemaphoreGive(testSem);
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
      String clientId = "device-raspberry-" + String(random(0xffff), HEX);
      if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
        client.subscribe(loginSuccessEsp);
        client.subscribe(coffeeDispensingEsp);
        client.subscribe(testTopic);
      }
    }

    client.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void loginTask(void* pvParameters) {
  for (;;) {
    if (xSemaphoreTake(loginSem, portMAX_DELAY)) {
      isLoggedIn = true;
    }
  }
}

void coffeeTask(void* pvParameters) {
  for (;;) {
    if (xSemaphoreTake(coffeeSem, portMAX_DELAY)) {
      if (!isLoggedIn) continue;
      retrieveCoffeeCapsule();
      isLoggedIn = false;
    }
  }
}

void testTask(void* pvParameters) {
  for (;;) {
    if (xSemaphoreTake(testSem, portMAX_DELAY)) {
      Serial.println(diff_asm(1, 1));
    }
  }
}

void setup() {
  Serial.begin(9600);
  delay(2000);

  WiFi.mode(WIFI_STA);

  //TODO: change to esp32 api (but this works somehow)
  raspClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");

  servo.setPeriodHertz(50);
  servo.attach(25);

  loginSem  = xSemaphoreCreateBinary();
  coffeeSem = xSemaphoreCreateBinary();
  testSem   = xSemaphoreCreateBinary();

  xTaskCreatePinnedToCore(wifiMqttTask, "WiFiMQTT", 4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(loginTask,   "LoginTask", 2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(coffeeTask,  "CoffeeTask",2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(testTask,    "TestTask",  2048, NULL, 1, NULL, 1);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
