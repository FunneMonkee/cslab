#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

struct LogEntry {
  long long timestamp;
  double id;
};

//---- WiFi settings
const char* ssid = "Sousa";
const char* password = "olaola123";
//---- MQTT Broker settings
const char* mqtt_server = "5f479f2b79404567899d28e682008d15.s1.eu.hivemq.cloud";
const char* mqtt_username = "RaspberryPico";
const char* mqtt_password = "Raspberrypico2isep";

const char* loginSuccessEsp = "/coffee/esp32/auth/login_success";
const char* coffeeDispensingEsp = "/coffee/esp32/dispense/requested";
const char* test = "/test";
const int mqtt_port = 8883;

//---- Log Size
const int LOG_SIZE = 100;

const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

bool isLoggedIn = false;

int logIndex = 0;
int readIndex = 0;
int reconnectLoop = 5;

Servo servo;


WiFiClientSecure raspClient;
PubSubClient client(raspClient);
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
  raspClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
  servo.setPeriodHertz(50);
  servo.attach(25);
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
    value++;
  }


  client.loop();
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

//=======================================
// This void is called every time we have a message from the broker
void callback(char* topic, byte* payload, unsigned int length) {
  if(strcmp(topic, test) == 0){
    Serial.println(diff_asm(1,1));
  }
  else if (strcmp(topic, loginSuccessEsp) == 0) {
    isLoggedIn = true;
    Serial.println("Logged in");
  } else if (strcmp(topic, coffeeDispensingEsp) == 0 && isLoggedIn) {
    retrieveCoffeeCapsule();
  }
  Serial.print("Callback - ");
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

void retrieveCoffeeCapsule() {
  servo.write(180);  // sets the servo position according to the scaled value
  delay(250);
  servo.write(0);
  delay(250);
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

//======================================= publising as string
void publishMessage(const char* topic, String payload, boolean retained) {
  if (client.publish(topic, payload.c_str(), true))
    Serial.println("Message published [" + String(topic) + "]: " + payload);
}
