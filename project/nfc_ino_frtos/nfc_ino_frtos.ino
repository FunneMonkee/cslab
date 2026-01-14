#include <WiFi.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "UUID.h"
#include "time.h"

const char* ssid = "Sousa";
const char* password = "olaola123";

const char* httpServer = "http://172.20.10.2:8080";

const char* mqtt_server   = "5f479f2b79404567899d28e682008d15.s1.eu.hivemq.cloud";
const char* mqtt_username = "ESP32";
const char* mqtt_password = "Esp32isep";
const int   mqtt_port     = 8883;

const char* loginAttemptEsp     = "/coffee/esp32/auth/login_attempt";
const char* loginSuccessEsp     = "/coffee/esp32/auth/login_success";
const char* loginFailedEsp     = "/coffee/esp32/auth/login_failed";
const char* logoutEsp     = "/coffee/esp32/auth/logout";
const char* logoutFromDispenserEsp     = "/coffee/esp32/auth/logoutDispense";
const char* dispenseCompletedEsp = "/coffee/esp32/dispense/completed";
const char* dispenseFailedEsp = "/coffee/esp32/dispense/failed";
const char* dispenseRequestedEsp = "/coffee/esp32/dispense/requested";
const char* dispenseCancelEsp = "/coffee/esp32/dispense/canceled";
const char* requestRefillingEsp = "/coffee/esp32/refill/requested";
const char* refillingCompletedEsp = "/coffee/esp32/refill/completed";
const char* refillingCanceledEsp = "/coffee/esp32/refill/canceled";
const char* refillingFailedEsp = "/coffee/esp32/refill/failed";
const char* alertLowStockEsp = "/coffee/esp32/status/low"; //adicionar logica, parse do json e verificar o count
const char* alertFullStockEsp = "/coffee/esp32/status/full";


const char* loginAttemptEvent = "LoginAttempt";
const char* loginSuccessEvent = "LoginSuccess";
const char* logoutEvent = "Logout";
const char* loginFailedEvent = "LoginFailed";

const char* deviceId = "esp32";

const int authButtonPin   = 25;
const int coffeeButtonPin = 21;
const int coffeeCancelButtonPin = 22;
const int refillButtonPin = 16;
const int cancelRefillButtonPin = 17;

const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

const uint32_t DEBOUNCE_MS = 200;

const byte BLOCK_NUMBER = 4;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;


MFRC522::MIFARE_Key key;

MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

WiFiClientSecure espClient;
PubSubClient client(espClient);
UUID uuid;

volatile bool isLoggedIn = false;
volatile bool isMaintenance = false;
volatile bool isDispenseCompleted = false;
volatile bool isCoffeeRequested = false;

SemaphoreHandle_t coffeeButtonSem;
SemaphoreHandle_t coffeeDispenseFailedSem;
SemaphoreHandle_t coffeeButtonCanceledSem;
SemaphoreHandle_t refillButtonSem;
SemaphoreHandle_t refillFailedButtonSem;
SemaphoreHandle_t cancelRefillButtonSem;
SemaphoreHandle_t logoutFromDispenserTaskSem;
SemaphoreHandle_t alertLowStockSem;
SemaphoreHandle_t logoutSem;

volatile uint32_t lastAuthISR = 0;
volatile uint32_t lastCoffeeISR = 0;
volatile uint32_t coffeeCanceledISR = 0;
volatile uint32_t refillISR = 0;
volatile uint32_t cancelRefillISR = 0;

volatile uint32_t companyId = 0;

QueueHandle_t mqttQueue;
TimerHandle_t loginTimer;
TimerHandle_t untilRefillFinishesTimer;
uint64_t loginTimerValue = 15000;
uint64_t untilRefillFinishesTimerValue = 60000;


struct MqttMessage {
    char topic[128];
    char payload[512];
};


////////////////////////////////////////////////////////////// HELPER FUNCTIONS ///////////////////////////////////////////////////////////////////

void restartTimer(TimerHandle_t timer){
    if (xTimerIsTimerActive(timer) == pdTRUE) {
        xTimerStop(timer, 0);
    }
    xTimerStart(timer, 0);
}

int parseJsonPayload(const char* payload){
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.f_str());
      return -1;
  }
  if(!doc.containsKey("payload")){
    Serial.println("Error parsing payload, does not exist");
    return -1;
  }
  else {
    JsonArray arr = doc["payload"].as<JsonArray>();
    if (arr.size() == 0) return -1;

    return arr[0].as<int>();
  }
}

void cleanup() {
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void criticalErrorHandler(){
  Serial.println("CRITICAL ERROR");
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000)); // allow WDT to feed
  }
}

void configCardKey() {
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
}

void timerCallback(TimerHandle_t xTimer){
  Serial.println("Timer callback executing");
  xSemaphoreGive(logoutSem);
}


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
  if(strcmp(topic, dispenseCompletedEsp) == 0){
    postMessageIntoQueueFromISR(mqttQueue, topic, payload, length);
  }
  else if(strcmp(topic, dispenseFailedEsp) == 0){
    xSemaphoreGive(coffeeDispenseFailedSem);
  }
  else if(strcmp(topic, logoutFromDispenserEsp) == 0 ||
          strcmp(topic, refillingCompletedEsp) == 0){
    xSemaphoreGive(logoutFromDispenserTaskSem);
  }
  else if(strcmp(topic, refillingFailedEsp) == 0){
    xSemaphoreGive(refillFailedButtonSem);
  }
  else if(strcmp(topic, alertFullStockEsp) == 0){
    xSemaphoreGive(refillFailedButtonSem);
  }
  else if(strcmp(topic, alertLowStockEsp) == 0){
    postMessageIntoQueueFromISR(mqttQueue, topic, payload, length);
  }
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}


void postMessageIntoQueue(const QueueHandle_t queue, const char* topic, byte * payload, unsigned int length){
    MqttMessage msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';

    unsigned int copyLength = length < sizeof(msg.payload) - 1 ? length : sizeof(msg.payload) - 1;
    memcpy(msg.payload, payload, copyLength);
    msg.payload[copyLength] = '\0';  // add null terminator
    if (queue != NULL) {
        if (xQueueSend(queue, &msg, 0) != pdTRUE) {
          Serial.println("Queue full, msg not sent");
        }
    }
}

void postMessageIntoQueueFromISR(QueueHandle_t queue, const char* topic, byte* payload, unsigned int length){
    MqttMessage msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';

    unsigned int copyLength = length < sizeof(msg.payload) - 1 ? length : sizeof(msg.payload) - 1;
    memcpy(msg.payload, payload, copyLength);
    msg.payload[copyLength] = '\0';

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (queue != NULL) {
        if (xQueueSendFromISR(queue, &msg, &xHigherPriorityTaskWoken) != pdTRUE) {
        }
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void IRAM_ATTR coffeeButtonISR() {
  TickType_t now = xTaskGetTickCountFromISR(); 
  if (now - lastCoffeeISR > pdMS_TO_TICKS(DEBOUNCE_MS)) {
    lastCoffeeISR = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(coffeeButtonSem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}

void IRAM_ATTR coffeeCanceledButtonISR() {
  TickType_t now = xTaskGetTickCountFromISR(); 
  if (now - coffeeCanceledISR > pdMS_TO_TICKS(DEBOUNCE_MS)) {
    coffeeCanceledISR = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(coffeeButtonCanceledSem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}

void IRAM_ATTR refillButtonISR() {
  TickType_t now = xTaskGetTickCountFromISR(); 
  if (now - refillISR > pdMS_TO_TICKS(DEBOUNCE_MS)) {
    refillISR = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(refillButtonSem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}

void IRAM_ATTR cancelRefillButtonISR() {
  TickType_t now = xTaskGetTickCountFromISR(); 
  if (now - cancelRefillISR > pdMS_TO_TICKS(DEBOUNCE_MS)) {
    cancelRefillISR = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(cancelRefillButtonSem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}
////////////////////////////////////////////////////////////////// AUTH //////////////////////////////////////////////////////////////////////////

bool processCardRead() {
  if (!mfrc522.PICC_IsNewCardPresent()) return false;
  if (!mfrc522.PICC_ReadCardSerial()) return false;
  Serial.println("Card detected!");
  auto authStatus = mfrc522.PCD_Authenticate(
      0x60,
      BLOCK_NUMBER,
      &key,
      &(mfrc522.uid)
  );
  if (authStatus != 0) {
    Serial.print("Authentication failed before read! Code: ");
    Serial.println(authStatus);
    return false;
  }
  byte buffer[18] = {0};
  byte size = sizeof(buffer);
  byte readStatus = mfrc522.MIFARE_Read(BLOCK_NUMBER, buffer, &size);
  if (readStatus != 0) {
    Serial.print("Read failed! Code: ");
    Serial.println(readStatus);
    return false;
  }
  cleanup();
  companyId = ((uint16_t)buffer[0] << 8) | buffer[1];
  Serial.print("CompanyID read from card: ");
  Serial.println(companyId);
  loginAttempt();
  vTaskDelay(pdMS_TO_TICKS(50));
  return verifyCompanyID();
}

bool verifyCompanyID(){
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(httpServer) + "/users/" + String(companyId);
    Serial.println(url);
    http.begin(url); // URL
    int httpCode = http.GET();
    if (httpCode > 0) {
      return checkCardPayload(http.getString());
    } else {
      Serial.println("HTTP GET failed");
      loginFailed("TIMEOUT");
    }
    http.end();
  }
  return false;
}

void postDispenseComplete(const char * payload){
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(httpServer) + "/dispenses";
    Serial.println(url);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(String(payload));
    vTaskDelay(pdMS_TO_TICKS(1));
    if (httpCode > 0 && httpCode < 300) {
      Serial.println("HTTP POST request done");
    } else {
      Serial.println("HTTP POST failed");
      Serial.println(httpCode);
    }
    http.end();
  }
}

bool checkCardPayload(const String payload) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);

  Serial.println(payload);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    return false;
  }
  JsonArray arr = doc.as<JsonArray>();

  JsonObject user = arr[0];
  
  const char* nfc_id = user["nfc_id"];
  const char* permission = user["permission"];
  char * endString;

  if(nfc_id == NULL || permission == NULL){
    loginFailed("INVALID_CARD");
    return false;
  }

  if(strcmp(permission, "maintenance") == 0){
    isMaintenance = true;
  }

  companyId = strtol(nfc_id, &endString, 10);

  if (nfc_id == endString) {
    Serial.println("Error: nfc_id is not a valid number");
    return false;
  }
  if (*endString != '\0') {
    Serial.println("Error: nfc_id has invalid characters");
    return false;
  }
  return true;
}

void loginAttempt(){
  StaticJsonDocument<64> payloadDoc;
  JsonArray payload = payloadDoc.to<JsonArray>();

  payload.add(companyId);
  processJsonAndPublishTopic(loginAttemptEvent, "INFO", loginAttemptEsp, payload);
}

void login(){
  StaticJsonDocument<64> payloadDoc;
  JsonArray payload = payloadDoc.to<JsonArray>();

  payload.add(companyId);
  processJsonAndPublishTopic(loginSuccessEvent, "INFO", loginSuccessEsp,payload);
  isLoggedIn = true;
}

void logout() {
  StaticJsonDocument<64> payloadDoc;
  JsonArray payload = payloadDoc.to<JsonArray>();

  payload.add(companyId);
  processJsonAndPublishTopic(logoutEvent, "INFO", logoutEsp, payload);
  isLoggedIn = false;
}

void loginFailed(const char* reason) {
  StaticJsonDocument<64> payloadDoc;
  JsonArray payload = payloadDoc.to<JsonArray>();

  payload.add(companyId);
  payload.add(reason);
  processJsonAndPublishTopic(loginFailedEvent, "WARN", loginFailedEsp, payload);
}

void processJsonAndPublishTopic(const char* event, const char* severity, const char* topic, JsonArray payload) {
  DynamicJsonDocument doc = generateJson(event, severity, &payload);
  char buffer[512];
  serializeJson(doc, buffer);
  publishMessage(topic, buffer);
}

void processJsonAndPublishTopic(const char* event, const char* severity, const char* topic) {
  DynamicJsonDocument doc = generateJson(event, severity, nullptr);
  char buffer[512];
  serializeJson(doc, buffer);
  Serial.println(buffer);
  Serial.println(topic);
  publishMessage(topic, buffer);
}

DynamicJsonDocument generateJson(
  const char* event,
  const char* severity,
  JsonArray* payload) {
  DynamicJsonDocument doc(512);
  uuid.generate();
  doc["timestamp"] = getLocTime();
  doc["id"] = deviceId;
  doc["uuid"] = uuid.toCharArray();
  doc["severity"] = severity;
  doc["eventType"] = event;
  if(payload != nullptr){
    doc["payload"].set(*payload); 
  }
  return doc;
}

void authTask(void* pvParameters) {
  for (;;) {
    if(!isLoggedIn){
     if(processCardRead()){
        login();
        xTimerStart(loginTimer, 0);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

//////////////////////////////////////////// COFFEE /////////////////////////////////////////////////////

void coffeeTask(void* pvParameters) {
  for (;;) {
    if (xSemaphoreTake(coffeeButtonSem, portMAX_DELAY)) {
      Serial.println("CoffeeTask");
      if (isLoggedIn){
        if (xTimerIsTimerActive(loginTimer) == pdTRUE) {
          xTimerStop(loginTimer, 0);
        }
        if (xTimerIsTimerActive(untilRefillFinishesTimer) == pdFALSE) {
          xTimerStart(untilRefillFinishesTimer, 0);
        }
        StaticJsonDocument<256> doc;
        doc["timestamp"] = getLocTime();
        doc["id"] = deviceId;
        doc["severity"] = "INFO";
        doc["eventType"] = "coffeeDispensing";
        doc["flavor"] = "BlackCoffeeNespresso";
        char buffer[256];
        serializeJson(doc, buffer);
        publishMessage(dispenseRequestedEsp, buffer);
        
        isCoffeeRequested = true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

////////////////////////////////////////////////////////////////// WIFI //////////////////////////////////////////////////////////////////////////

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
      if (client.connect(clientId.c_str(), mqtt_username, mqtt_password, nullptr, 0, false, nullptr, true)){
        client.subscribe(logoutFromDispenserEsp);
        client.subscribe(dispenseCompletedEsp);
        client.subscribe(dispenseFailedEsp);
        client.subscribe(refillingCompletedEsp);
        client.subscribe(alertLowStockEsp);
        client.subscribe(alertFullStockEsp);
      }
    }
    client.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

///////////////////////////////////////////////////////////////// COFFECOMPLETION ////////////////////////////////////////////////////////////

void coffeeDispenseFailedTask(void* pvParameters){
  for(;;) {
        if(xSemaphoreTake(coffeeDispenseFailedSem, portMAX_DELAY)) {
          criticalErrorHandler();
        }
    }
  vTaskDelay(pdMS_TO_TICKS(10));
}
//////////////////////////////////////////////////////////////CANCELTASK ///////////////////////////////////////////////////


void cancelDispense(void* pvParameters){
  for(;;){
    if(isLoggedIn){
      if(xSemaphoreTake(coffeeButtonCanceledSem, portMAX_DELAY)){
        if(!isDispenseCompleted){
          processJsonAndPublishTopic("DispenseCanceled", "WARN", dispenseCancelEsp);
          restartTimer(loginTimer);
          xTimerStop(untilRefillFinishesTimer, 0);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

//////////////////////////////////////////////////////////////Refill ///////////////////////////////////////////////////

void refillTask(void* pvParameters){
  for(;;){
    if(isLoggedIn && isMaintenance){
      if(xSemaphoreTake(refillButtonSem, portMAX_DELAY)){
          Serial.println("Refill requested");
          xTimerStop(loginTimer, 0);
          processJsonAndPublishTopic("RequestedRefill", "INFO", requestRefillingEsp);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void cancelRefillTask(void* pvParameters){
  for(;;){
    if(isLoggedIn && isMaintenance){
      if(xSemaphoreTake(cancelRefillButtonSem, portMAX_DELAY)){
          processJsonAndPublishTopic("RequestedCancelRefill", "WARN", refillingCanceledEsp);
          restartTimer(loginTimer);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void failedRefillTask(void* pvParameters){
  for(;;){
    if(xSemaphoreTake(refillFailedButtonSem, portMAX_DELAY)) {
      criticalErrorHandler();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

//////////////////////////////////////////////////////////////logout ///////////////////////////////////////////////////

void logoutFromDispenserTask(void* pvParameters){
  for(;;){
    if(xSemaphoreTake(logoutFromDispenserTaskSem, portMAX_DELAY)) {
        Serial.println("Logout received from dispenser");
        isLoggedIn = false;
        if(isMaintenance){
          isMaintenance = false;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void logoutTask(void * pvParameters){
  for(;;){
    if(xSemaphoreTake(logoutSem, portMAX_DELAY)) {
        Serial.println("Logout received");
        logout();
        if(isMaintenance){
          isMaintenance = false;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

////////////////////////////////////////////////////////////// PROCESS INFORMATION //////////////////////////////////////////////////


void mqttProcessingTask(void* pvParameters) {
    MqttMessage msg;
    for (;;) {
        if (xQueueReceive(mqttQueue, &msg, portMAX_DELAY) == pdTRUE) {
            if (strcmp(msg.topic, alertLowStockEsp) == 0) {
                if(parseJsonPayload(msg.payload) == 0){
                  Serial.println("There's no coffee capsules available");
                }
            }
            else if (strcmp(msg.topic, dispenseCompletedEsp) == 0) {
                postDispenseComplete(msg.payload);
                xTimerStop(untilRefillFinishesTimer, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(coffeeButtonPin, INPUT_PULLUP);
  pinMode(coffeeCancelButtonPin, INPUT_PULLUP);
  pinMode(refillButtonPin, INPUT_PULLUP);
  pinMode(cancelRefillButtonPin, INPUT_PULLUP);
  
  coffeeButtonSem = xSemaphoreCreateBinary();
  coffeeDispenseFailedSem = xSemaphoreCreateBinary();
  coffeeButtonCanceledSem = xSemaphoreCreateBinary();
  refillButtonSem = xSemaphoreCreateBinary();
  cancelRefillButtonSem = xSemaphoreCreateBinary();
  refillFailedButtonSem = xSemaphoreCreateBinary();
  logoutFromDispenserTaskSem = xSemaphoreCreateBinary();
  logoutSem = xSemaphoreCreateBinary();

  loginTimer = xTimerCreate(
    "LoginTimer",
    pdMS_TO_TICKS(loginTimerValue),
    pdFALSE,
    NULL,
    timerCallback
  );

  untilRefillFinishesTimer = xTimerCreate(
    "UntilRefillFinishesTimer",
    pdMS_TO_TICKS(untilRefillFinishesTimerValue),
    pdFALSE,
    NULL,
    timerCallback
  );

  mqttQueue = xQueueCreate(100, sizeof(MqttMessage));
  if (!mqttQueue) {
    Serial.println("Failed to create MQTT queues!");
    while(1); // stop
}
 
  mfrc522.PCD_Init();
  configCardKey();

  WiFi.mode(WIFI_STA);
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
  uuid.seed(2);

  attachInterrupt(coffeeButtonPin, coffeeButtonISR, RISING);

  attachInterrupt(coffeeCancelButtonPin, coffeeCanceledButtonISR, RISING);

  attachInterrupt(refillButtonPin, refillButtonISR, RISING);

  attachInterrupt(cancelRefillButtonPin, cancelRefillButtonISR, RISING);

  xTaskCreatePinnedToCore(wifiMqttTask, "WiFiMQTT", 8192, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(authTask, "AuthTask", 12288, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(coffeeTask, "CoffeeTask", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(cancelDispense, "CancelDispense", 4096, NULL, 0, NULL, 0);
  xTaskCreatePinnedToCore(coffeeDispenseFailedTask, "coffeeDispenseFailedTask", 1024, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(refillTask, "refillTask", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(cancelRefillTask, "cancelRefillTask", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(failedRefillTask, "FailedRefillTask", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(logoutFromDispenserTask, "LogoutFromDispenserTask", 2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(logoutTask, "LogoutTask", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(mqttProcessingTask, "ProcessInformation", 8192, NULL, 2, NULL, 0);

  
  if (loginTimer == NULL) {
    Serial.println("Failed to create timer");
  }
}

void loop() {
  //TODO: not sure if needed, might just work with an empty loop()
  vTaskDelay(portMAX_DELAY);
}
