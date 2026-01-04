#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// Log settings
struct LogEntry {
  long long timestamp;
  double id;
};
const int LOG_SIZE = 100;

// WiFi settings
const char* ssid = "Pinto";
const char* password = "uJM9X8zG2q";

// MQTT Broker settings
const char* mqtt_server = "5f479f2b79404567899d28e682008d15.s1.eu.hivemq.cloud";
const char* mqtt_username = "RaspberryPico";
const char* mqtt_password = "Raspberrypico2isep";
const int mqtt_port = 8883;
WiFiClientSecure mqttClient;
PubSubClient client(mqttClient);

// MQTT Topics
const char* loginSuccessEsp = "/coffee/esp32/auth/login_success";
const char* coffeeDispensingEsp = "/coffee/esp32/dispense/requested";
const char* test = "/test";

// Time settings
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// Declare task handle
TaskHandle_t MoveServoTaskHandle = NULL;
TaskHandle_t MQTTReconnectTaskHandle = NULL;

// Semaphores
SemaphoreHandle_t moveServoTaskSem = NULL;

// Servos
Servo servo;

// Global vars
bool isLoggedIn = true;
int logIndex = 0;
int readIndex = 0;
int reconnectLoop = 5;
bool wifiConnection = false;

// Tasks
//TODO: wifi reconnect hogged both cores (even with higher prio servo task)

void MQTTReconnectTask(void* parameter) {
  for (;;) {
    if (!client.connected()) {
      Serial.println("Attempting MQTT connection…");
      String clientId = "device-raspberry";
      clientId += String(random(0xffff), HEX);
      if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
        Serial.println("connected");
        client.subscribe(loginSuccessEsp);
        client.subscribe(coffeeDispensingEsp);
        client.subscribe(test);
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
      Serial.println("moving servo..");
      servo.write(180);
      delay(250);
      servo.write(0);
      delay(250);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("callback");
  if (strcmp(topic, test) == 0) {
    //Serial.println(diff_asm(1, 1));
  } else if (strcmp(topic, loginSuccessEsp) == 0) {
    isLoggedIn = true;
    Serial.println("Logged in");
  } else if (strcmp(topic, coffeeDispensingEsp) == 0 && isLoggedIn) {
    Serial.println("give sem");
    xSemaphoreGive(moveServoTaskSem);
  }
  Serial.print("Callback - ");
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

void setup() {
  delay(5000);
  Serial.begin(115200);
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

  moveServoTaskSem = xSemaphoreCreateBinary();

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
    1,                     // Priority
    &MoveServoTaskHandle,  // Task handle
    1                      // Core 1
  );
}

void loop() {
  // Empty because FreeRTOS scheduler runs the task
}
