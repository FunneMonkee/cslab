#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "UUID.h"
#include "time.h"
#include <ESP32Servo.h>

// WiFi settings
const char* ssid = "iPhone Luis";
const char* password = "conaconacona";

// MQTT settings
const char* mqtt_server = "5f479f2b79404567899d28e682008d15.s1.eu.hivemq.cloud";
const char* mqtt_username = "RaspberryPico";
const char* mqtt_password = "Raspberrypico2isep";
const int mqtt_port = 8883;
WiFiClientSecure mqttClient;
PubSubClient client(mqttClient);
const char* deviceId = "esp32";

// MQTT Topics
// Consume
const char* loginSuccessEsp = "/coffee/esp32/auth/login_success";
//Dispensing
const char* coffeeDispensingEsp = "/coffee/esp32/dispense/requested";
const char* coffeeDispensingCanceledEsp = "/coffee/esp32/dispense/canceled";
// Refilling
const char* coffeeRefillingEsp = "/coffee/esp32/refill/requested";
const char* coffeeRefillingCanceledEsp = "/coffee/esp32/refill/canceled";
// Consume/Publish
const char* logoutEsp = "/coffee/esp32/auth/logout";
// Publish
// Dispensing
const char* coffeeDispensingStartedEsp = "/coffee/esp32/dispense/started";
const char* coffeeDispensingCompletedEsp = "/coffee/esp32/dispense/completed";
const char* coffeeDispensingFailedEsp = "/coffee/esp32/dispense/failed";
const char* coffeeDispensingCanceledFailedEsp = "/coffee/esp32/dispense/canceled_failed";
// Dispensing
const char* coffeeRefillingStartedEsp = "/coffee/esp32/refill/started";
const char* coffeeRefillingCompletedEsp = "/coffee/esp32/refill/completed";
const char* coffeeRefillingFailedEsp = "/coffee/esp32/refill/failed";
const char* coffeeRefillingCanceledFailedEsp = "/coffee/esp32/refill/canceled_failed";
// Logging
const char* statusMetricsEsp = "/coffee/esp32/status/metrics";
const char* alertLowStockEsp = "/coffee/esp32/status/low";

// Time settings
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// Declare task handle
TaskHandle_t MoveServoTaskHandle = NULL;
TaskHandle_t MoveRefillServoTaskHandle = NULL;
TaskHandle_t MQTTReconnectTaskHandle = NULL;
TaskHandle_t LogoutTaskHandle = NULL;
TaskHandle_t LoginTaskHandle = NULL;
TaskHandle_t CancelTaskHandle = NULL;
TaskHandle_t LoggingTaskHandle = NULL;

// Semaphores
SemaphoreHandle_t moveServoTaskSem = NULL;
SemaphoreHandle_t moveServoRefillTaskSem = NULL;
SemaphoreHandle_t logoutTaskSem = NULL;
SemaphoreHandle_t loginTaskSem = NULL;
SemaphoreHandle_t cancelTaskSem = NULL;

// Servos
Servo servo;

// Global vars
bool isLoggedIn = false;
bool isDispensing = false;
bool isRefilling = false;
bool hasReceivedCancel = false;
bool hasTimedOut = false;
int logIndex = 0;
int readIndex = 0;
int reconnectLoop = 5;
bool wifiConnection = false;
int dropSensor1 = 22;
int dropSensor2 = 21;
int capsuleCount = 5;
UUID uuid;
char* userId = "no-user";
int upTime = 0;

// Enums
enum Severity {
  INFO,
  WARN,
  ERROR,
  CRITICAL
};

enum Event {
  LOGOUT,
  DISPENSE_STARTED,
  DISPENSE_COMPLETED,
  DISPENSE_CANCELED,
  DISPENSE_FAILED,
  REFILL_STARTED,
  REFILL_COMPLETED,
  REFILL_CANCELED,
  REFILL_FAILED,
  TIMEOUT,
  LOGGING,
  STOCK
};

const char* SeverityStrings[4] = {
  "info",
  "warn",
  "error",
  "critical"
};

const char* EventStrings[12] = {
  "logout",
  "dispense_started",
  "dispense_completed",
  "dispense_canceled",
  "dispense_failed",
  "refill_started",
  "refill_completed",
  "refill_canceled",
  "refill_failed",
  "timeout",
  "logging",
  "stock"
};

// Methods
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
  if (strcmp(topic, loginSuccessEsp) == 0) {
    xSemaphoreGive(loginTaskSem);
  } else if (strcmp(topic, logoutEsp) == 0) {
    xSemaphoreGive(logoutTaskSem);
  } else if (strcmp(topic, coffeeDispensingEsp) == 0 && isLoggedIn && !isDispensing) {
    xSemaphoreGive(moveServoTaskSem);
  } else if (strcmp(topic, coffeeRefillingEsp) == 0 && isLoggedIn && !isRefilling) {
    xSemaphoreGive(moveServoRefillTaskSem);
  } else if (strcmp(topic, coffeeDispensingCanceledEsp) == 0 && isDispensing) {
    xSemaphoreGive(cancelTaskSem);
  } else if (strcmp(topic, coffeeRefillingCanceledEsp) == 0 && isRefilling) {
    xSemaphoreGive(cancelTaskSem);
  }
  Serial.print("Callback - ");
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

bool readServoMovSensor() {
  TickType_t startTick = xTaskGetTickCount();
  TickType_t timeoutTicks = pdMS_TO_TICKS(8000);

  bool sensor1Seen = false;
  bool sensor2Seen = false;

  while ((xTaskGetTickCount() - startTick) < timeoutTicks) {
    if (hasReceivedCancel && (!sensor1Seen && !sensor2Seen)) {
      return false;
    } else if (hasReceivedCancel && sensor1Seen && sensor2Seen) {
      auto doc = generateErrorJson(Event::DISPENSE_CANCELED);
      char bufferJson[256];
      serializeJson(doc, bufferJson);
      publishMessage(coffeeDispensingCanceledFailedEsp, bufferJson);
      Serial.println("dispense canceled failed");
    }

    if (digitalRead(dropSensor1) == HIGH) {
      sensor1Seen = true;
    }

    if (digitalRead(dropSensor2) == HIGH) {
      sensor2Seen = true;
    }

    if (sensor1Seen && sensor2Seen) {
      Serial.println("Both sensors triggered");
      return true;
    }
    delay(10);
  }
  hasTimedOut = true;
  return false;
}

bool readServoRefillMovSensor() {
  TickType_t startTick = xTaskGetTickCount();
  TickType_t timeoutTicks = pdMS_TO_TICKS(10000);

  bool sensor1Seen = false;
  bool sensor2Seen = false;

  while ((xTaskGetTickCount() - startTick) < timeoutTicks) {
    if (hasReceivedCancel && (!sensor1Seen && !sensor2Seen)) {
      return false;
    } else if (hasReceivedCancel && sensor1Seen && sensor2Seen) {
      auto doc = generateErrorJson(Event::REFILL_CANCELED);
      char bufferJson[256];
      serializeJson(doc, bufferJson);
      publishMessage(coffeeRefillingCanceledFailedEsp, bufferJson);
      Serial.println("refill canceled failed");
    }

    if (digitalRead(dropSensor1) == HIGH) {
      sensor1Seen = true;
    }

    if (digitalRead(dropSensor2) == HIGH) {
      sensor2Seen = true;
    }

    if (sensor1Seen && sensor2Seen) {
      Serial.println("Both sensors triggered");
      return true;
    }
    delay(10);
  }
  hasTimedOut = true;
  return false;
}

void alertStock() {
  StaticJsonDocument<64> payloadDoc; 
  JsonArray payload = emptyDoc.createNestedArray();
  payload.add(capsuleCount)
  const auto doc = generateInfoJson(Event::STOCK, payload);
  char bufferJson[256];
  serializeJson(doc, bufferJson);
  publishMessage(alertLowStockEsp, bufferJson);
  Serial.println("alert low stock");
}

// JSON
JsonDocument generateInfoJson(Event event, JsonArray payload) {
  JsonDocument doc;
  FillDefaultProperties(doc);
  doc["eventType"] = EventStrings[event];
  doc["severity"] = SeverityStrings[Severity::INFO];
  doc["payload"].set(payload);

  return doc;
}

JsonDocument generateWarningJson(Event event) {
  JsonDocument doc;
  FillDefaultProperties(doc);
  doc["eventType"] = EventStrings[event];
  doc["severity"] = SeverityStrings[Severity::WARN];

  return doc;
}

JsonDocument generateErrorJson(Event event) {
  JsonDocument doc;
  FillDefaultProperties(doc);
  doc["eventType"] = EventStrings[event];
  doc["severity"] = SeverityStrings[Severity::ERROR];

  return doc;
}

JsonDocument generateCriticalJson(Event event) {
  JsonDocument doc;
  FillDefaultProperties(doc);
  doc["eventType"] = EventStrings[event];
  doc["severity"] = SeverityStrings[Severity::CRITICAL];

  return doc;
}

void FillDefaultProperties(JsonDocument doc) {
  uuid.generate();
  doc["timestamp"] = getLocTime();
  doc["id"] = deviceId;
  doc["uuid"] = uuid.toCharArray();
  doc["userId"] = userId;
}

// Tasks
//TODO: wifi reconnect hogged both cores (even with higher prio servo task)

void MQTTReconnectTask(void* parameter) {
  for (;;) {
    if (!client.connected()) {
      Serial.println("Attempting MQTT connection…");
      String clientId = deviceId;
      clientId += String(random(0xffff), HEX);
      if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
        Serial.println("connected");
        client.subscribe(loginSuccessEsp);
        client.subscribe(logoutEsp);
        client.subscribe(coffeeDispensingEsp);
        client.subscribe(coffeeDispensingCanceledEsp);
        client.subscribe(coffeeRefillingEsp);
        client.subscribe(coffeeRefillingCanceledEsp);
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
      }
    }

    client.loop();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void MoveServoTask(void* parameter) {
  for (;;) {
    if (xSemaphoreTake(moveServoTaskSem, portMAX_DELAY)) {
      if (capsuleCount > 0) {
        hasTimedOut = false;
        hasReceivedCancel = false;
        isDispensing = true;

        Serial.println("Dispense started..");
        StaticJsonDocument<64> emptyDoc; 
        JsonArray emptyArray = emptyDoc.createNestedArray();
        const auto doc = generateInfoJson(Event::DISPENSE_STARTED, emptyArray);
        char bufferJson[256];
        serializeJson(doc, bufferJson);
        publishMessage(coffeeDispensingStartedEsp, bufferJson);
        servo.write(180);
        delay(250);
        if (readServoMovSensor()) {
          capsuleCount--;
          if (capsuleCount < 3)
            alertStock();

          StaticJsonDocument<64> payloadDoc;
          JsonArray payload = payloadDoc.to<JsonArray>();

          payload.add(capsuleCount);

          const auto doc = generateInfoJson(Event::DISPENSE_COMPLETED, payload);
          char bufferJson[256];
          serializeJson(doc, bufferJson);
          publishMessage(coffeeDispensingCompletedEsp, bufferJson);
          servo.write(180);
          servo.write(0);
          delay(250);
          isDispensing = false;
          Serial.println("dispense completed");
        } else {
          auto doc = generateWarningJson(Event::DISPENSE_FAILED);

          if (hasReceivedCancel && !hasTimedOut) {
            doc = generateWarningJson(Event::DISPENSE_CANCELED);
            Serial.println("dispense canceled");
            isDispensing = false;
          } else if (hasTimedOut) {
            doc = generateCriticalJson(Event::TIMEOUT);
            Serial.println("dispense timeout");
          }
          char bufferJson[256];
          serializeJson(doc, bufferJson);
          publishMessage(coffeeDispensingFailedEsp, bufferJson);
          Serial.println("dispense failed");

          //simulate turn off
          vTaskDelay(pdMS_TO_TICKS(9999999));
        }
      } else
        alertStock();
    }
  }
}

void MoveRefillServoTask(void* parameter) {
  for (;;) {
    if (xSemaphoreTake(moveServoRefillTaskSem, portMAX_DELAY)) {
      if (capsuleCount < 10) {
        hasTimedOut = false;
        hasReceivedCancel = false;
        isRefilling = true;

        Serial.println("Refill started..");
        StaticJsonDocument<64> emptyDoc; 
        JsonArray emptyArray = emptyDoc.createNestedArray();
        auto doc = generateInfoJson(Event::REFILL_STARTED, emptyArray);
        char bufferJson[256];
        serializeJson(doc, bufferJson);
        publishMessage(coffeeRefillingStartedEsp, bufferJson);
        servo.write(180);
        delay(250);
        TickType_t startTick = xTaskGetTickCount();
        TickType_t timeoutTicks = pdMS_TO_TICKS(8000);

        while ((xTaskGetTickCount() - startTick) < timeoutTicks) {
          while (capsuleCount < 10) {
            if (readServoRefillMovSensor()) {
              capsuleCount++;
              Serial.println(capsuleCount);
            }
            if (hasReceivedCancel) {
              break;
            }
          }
        }
        if (capsuleCount == 10) {
          StaticJsonDocument<64> payloadDoc;
          JsonArray payload = payloadDoc.to<JsonArray>();

          payload.add(capsuleCount);

          const auto doc = generateInfoJson(Event::REFILL_COMPLETED, payload);
          char bufferJson[256];
          serializeJson(doc, bufferJson);
          publishMessage(coffeeRefillingCompletedEsp, bufferJson);
          servo.write(180);

          servo.write(0);
          delay(250);
          isRefilling = false;
          Serial.println("dispense completed");
        } else {
          doc = generateWarningJson(Event::REFILL_FAILED);

          if (hasReceivedCancel && !hasTimedOut) {
            doc = generateWarningJson(Event::REFILL_CANCELED);
            Serial.println("refill canceled");
            isRefilling = false;
          } else if (hasTimedOut) {
            doc = generateCriticalJson(Event::TIMEOUT);
            Serial.println("refill timeout");
          }

          bufferJson[256];
          serializeJson(doc, bufferJson);
          publishMessage(coffeeRefillingFailedEsp, bufferJson);
          Serial.println("refill failed");

          //simulate turn off
          vTaskDelay(pdMS_TO_TICKS(9999999));
        }
      }
    }
  }
}

void CancelTask(void* parameter) {
  for (;;) {
    if (xSemaphoreTake(cancelTaskSem, portMAX_DELAY)) {
      Serial.println("cancelling..");
      hasReceivedCancel = true;
      Serial.println("cancelled");
    }
  }
}

void LoginTask(void* parameter) {
  for (;;) {
    if (xSemaphoreTake(loginTaskSem, portMAX_DELAY)) {
      Serial.println("logging in..");
      isLoggedIn = true;
      Serial.println("logged in");
    }
  }
}

void LogoutTask(void* parameter) {
  for (;;) {
    if (xSemaphoreTake(logoutTaskSem, portMAX_DELAY)) {
      Serial.println("logging out..");
      StaticJsonDocument<64> emptyDoc; 
      JsonArray emptyArray = emptyDoc.createNestedArray();
      const auto doc = generateInfoJson(Event::LOGOUT, emptyArray);
      char bufferJson[256];
      serializeJson(doc, bufferJson);
      publishMessage(coffeeDispensingFailedEsp, bufferJson);
      isLoggedIn = false;
      Serial.println("logged out");
    }
  }
}

void LoggingTask(void* parameter) {
  for (;;) {
    Serial.println("logging status..");
    StaticJsonDocument<64> payloadDoc;
    JsonArray payload = payloadDoc.to<JsonArray>();

    payload.add(capsuleCount);

    const auto doc = generateInfoJson(Event::LOGGING, payload);
    char bufferJson[256];
    serializeJson(doc, bufferJson);
    publishMessage(statusMetricsEsp, bufferJson);
    Serial.println("logged");
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

void setup() {
  delay(5000);
  Serial.begin(115200);
  TickType_t startTick = xTaskGetTickCount();
  upTime = (unsigned int)startTick;
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
  mqttClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
  servo.setPeriodHertz(50);
  servo.attach(23);
  pinMode(dropSensor1, INPUT);
  pinMode(dropSensor2, INPUT);

  moveServoTaskSem = xSemaphoreCreateBinary();
  moveServoRefillTaskSem = xSemaphoreCreateBinary();
  loginTaskSem = xSemaphoreCreateBinary();
  logoutTaskSem = xSemaphoreCreateBinary();
  cancelTaskSem = xSemaphoreCreateBinary();

  // Network in Core 0
  xTaskCreatePinnedToCore(
    MQTTReconnectTask,
    "MQTTTReconnectTask",
    10000,
    NULL,
    1,
    &MQTTReconnectTaskHandle,
    0);

  xTaskCreatePinnedToCore(
    MoveServoTask,         // Task function
    "MoveServoTask",       // Task name
    10000,                 // Stack size (bytes)
    NULL,                  // Parameters
    4,                     // Priority
    &MoveServoTaskHandle,  // Task handle
    1                      // Core 1
  );

  xTaskCreatePinnedToCore(
    MoveRefillServoTask,
    "MoveRefillServoTask",
    10000,
    NULL,
    4,
    &MoveRefillServoTaskHandle,
    1);

  xTaskCreatePinnedToCore(
    LogoutTask,
    "LogoutTaskTask",
    10000,
    NULL,
    3,
    &LogoutTaskHandle,
    1);

  xTaskCreatePinnedToCore(
    LoginTask,
    "LoginTaskTask",
    10000,
    NULL,
    2,
    &LoginTaskHandle,
    1);

  xTaskCreatePinnedToCore(
    CancelTask,
    "CancelTask",
    10000,
    NULL,
    5,
    &CancelTaskHandle,
    1);

  xTaskCreatePinnedToCore(
    LoggingTask,
    "LoggingTask,",
    10000,
    NULL,
    1,
    &LoggingTaskHandle,
    1);
}

void loop() {
  // Empty because FreeRTOS scheduler runs the task
}
