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

const char* httpServer = "http://172.20.10.2:8080/users";

const char* mqtt_server   = "5f479f2b79404567899d28e682008d15.s1.eu.hivemq.cloud";
const char* mqtt_username = "ESP32";
const char* mqtt_password = "Esp32isep";
const int   mqtt_port     = 8883;

const char* loginAttemptEsp     = "/coffee/esp32/auth/login_attempt";
const char* loginSuccessEsp     = "/coffee/esp32/auth/login_success";
const char* loginFailedEsp     = "/coffee/esp32/auth/login_failed";
const char* logoutEsp     = "/coffee/esp32/auth/logout";
const char* dispenseCompletedEsp = "/coffee/esp32/dispense/completed";
const char* dispenseFailedEsp = "/coffee/esp32/dispense/failed";
const char* dispenseRequestedEsp = "/coffee/esp32/dispense/requested";
const char* dispenseCancelEsp = "/coffee/esp32/dispense/canceled";

const char* loginAttemptEvent = "LoginAttempt";
const char* loginSuccessEvent = "LoginSuccess";
const char* logoutEvent = "Logout";
const char* loginFailedEvent = "LoginFailed";

const char* deviceId = "esp32";

const int authButtonPin   = 25;
const int coffeeButtonPin = 21;
const int coffeeCancelButtonPin = 22;
const int refillButtonPin = 2;

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

bool isLoggedIn = false;
bool isDispenseCompleted = false;
bool isCoffeeRequested = false;

SemaphoreHandle_t coffeeButtonSem;
SemaphoreHandle_t coffeeDispenseCompletedSem;
SemaphoreHandle_t coffeeDispenseFailedSem;
SemaphoreHandle_t coffeeButtonCanceledSem;
SemaphoreHandle_t refillButtonSem;

volatile uint32_t lastAuthISR = 0;
volatile uint32_t lastCoffeeISR = 0;
volatile uint32_t coffeeCanceledISR = 0;
volatile uint32_t refillISR = 0;

volatile uint32_t companyId = 0;

TimerHandle_t loginTimer;
uint64_t timerValue = 3000;

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
    xSemaphoreGive(coffeeDispenseCompletedSem);
  }
  else if(strcmp(topic, dispenseFailedEsp) == 0){
    xSemaphoreGive(coffeeDispenseFailedSem);
  }
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
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

void IRAM_ATTR coffeeCanceledButtonISR() {
  uint32_t now = millis();
  if (now - lastCoffeeISR > DEBOUNCE_MS) {
    coffeeCanceledISR = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(coffeeButtonCanceledSem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}

void IRAM_ATTR refillButtonISR() {
  uint32_t now = millis();
  if (now - refillISR > DEBOUNCE_MS) {
    refillISR = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(refillButtonSem, &xHigherPriorityTaskWoken);
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
  companyId = ((uint16_t)buffer[0] << 8) | buffer[1];
  Serial.print("CompanyID read from card: ");
  Serial.println(companyId);
  loginAttempt();
  return verifyCompanyID();
}

bool verifyCompanyID(){
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(httpServer) + "/" + String(companyId);
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
  setLoginFlagValue(true);
}

void logout() {
  StaticJsonDocument<64> payloadDoc;
  JsonArray payload = payloadDoc.to<JsonArray>();

  payload.add(companyId);
  processJsonAndPublishTopic(logoutEvent, "INFO", logoutEsp, payload);
  setLoginFlagValue(false);
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
  publishMessage(topic, buffer);
}

bool getLoginFlagValue() {
  portENTER_CRITICAL(&mux);
  bool v = isLoggedIn;
  portEXIT_CRITICAL(&mux);
  return v;
}

DynamicJsonDocument generateJson(
  const char* event,
  const char* severity,
  JsonArray * payload) {
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
    if(!getLoginFlagValue()){
     if(processCardRead()){
        login();
        xTimerStart(loginTimer, 0);
      }
      cleanup();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

//////////////////////////////////////////// COFFEE /////////////////////////////////////////////////////

void coffeeTask(void* pvParameters) {
  for (;;) {
    if (xSemaphoreTake(coffeeButtonSem, portMAX_DELAY)) {
      Serial.println("Houve interrupção");
      if (getLoginFlagValue()){
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
      client.connect(clientId.c_str(), mqtt_username, mqtt_password);
    }
    client.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

///////////////////////////////////////////////////////////////// COFFECOMPLETION ////////////////////////////////////////////////////////////

void coffeeDispenseStatus(void* pvParameters){
  for(;;) {
    if (isCoffeeRequested){
      TickType_t startTick = xTaskGetTickCount();
      TickType_t timeoutTicks = pdMS_TO_TICKS(8000);
      while ((xTaskGetTickCount() - startTick) < timeoutTicks) {
        if(xSemaphoreTake(coffeeDispenseCompletedSem, portMAX_DELAY)){
          isDispenseCompleted = true;
        }
        if(xSemaphoreTake(coffeeDispenseFailedSem, portMAX_DELAY)) {
          criticalErrorHandler();
        }
      }
    }
  }
}

//////////////////////////////////////////////////////////////CANCELTASK ///////////////////////////////////////////////////


void cancelDispense(void* pvParameters){
  for(;;){
    if(xSemaphoreTake(coffeeButtonCanceledSem, portMAX_DELAY)){
      if(!isDispenseCompleted){
        processJsonAndPublishTopic("DispenseCanceled", "WARN", dispenseCancelEsp);
        xTimerStop(loginTimer, 0);
        xTimerStart(loginTimer, 0);
        }
    }
  }
}

//////////////////////////////////////////////////////////////Refill ///////////////////////////////////////////////////

void refillTask(void* pvParameters){
  for(;;){
    if(xSemaphoreTake(refillButtonSem, portMAX_DELAY)){
      if(!isDispenseCompleted){
        processJsonAndPublishTopic("DispenseCanceled", "WARN", dispenseCancelEsp);
        xTimerStop(loginTimer, 0);
        xTimerStart(loginTimer, 0);
        }
    }
  }
}


////////////////////////////////////////////////////////////// HELPER FUNCTIONS ///////////////////////////////////////////////////////////////////


void cleanup() {
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void criticalErrorHandler(){
  taskENTER_CRITICAL(&mux);
  for(;;){

  }
}

void configCardKey() {
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
}

void timerCallback(TimerHandle_t xTimer){
  Serial.println("Timer callback executed");
  logout();
  Serial.print("Login value: ");
  Serial.println(getLoginFlagValue());
}


void setDispenseFlagValue(bool value){
  portENTER_CRITICAL(&mux);
  isDispenseCompleted = value;
  portEXIT_CRITICAL(&mux);
}

void setLoginFlagValue(bool value){
  portENTER_CRITICAL(&mux);
  isLoggedIn = value;
  portEXIT_CRITICAL(&mux);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(coffeeButtonPin, INPUT_PULLUP);
  pinMode(coffeeCancelButtonPin, INPUT_PULLUP);
  
  coffeeButtonSem = xSemaphoreCreateBinary();
  coffeeDispenseCompletedSem = xSemaphoreCreateBinary();
  coffeeDispenseFailedSem = xSemaphoreCreateBinary();
  coffeeButtonCanceledSem = xSemaphoreCreateBinary();
  //logoutTaskSem = xSemaphoreCreateBinary();
 
  
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

  loginTimer = xTimerCreate(
    "LoginTimer",
    pdMS_TO_TICKS(timerValue),
    pdFALSE,
    NULL,
    timerCallback
  );

  xTaskCreatePinnedToCore(wifiMqttTask, "WiFiMQTT", 8192, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(authTask, "AuthTask", 12288, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(coffeeTask, "CoffeeTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(cancelDispense, "CancelDispense", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(coffeeDispenseStatus, "coffeeDispenseStatus", 8096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(refillTask, "refillTask", 4096, NULL, 2, NULL, 1);

  

  if (loginTimer == NULL) {
    Serial.println("Failed to create timer");
  }
}

void loop() {
  //TODO: not sure if needed, might just work with an empty loop()
  vTaskDelay(portMAX_DELAY);
}
