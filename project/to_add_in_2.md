Each machine will communicate within a local network.
timeout requirement
cancel requirement
espaco fisico 
espcaco local
login cache in spiffs, when user gets deleted on the rest api delete send mqtt do clean up any caching related to the user

ajeitar blackbox
- activity diagram com system e externos
- remover use cases que nao importam para o negocio
- moe
- explicar com texto os diagramas
- traceability the FR para QR

hardware to use for prototype
ESP32 Dev Module (ESP32-WROOM-32)
- Board A: Main control (sensing, actuation, comms)
- Board B: NFC-only controller

SG90 Micro Servo Motor
- Widely supported on ESP32
2 × SG90
- Bottom servo → capsule dispenser
- Top servo → refill lock mechanism

TCRT5000 Infrared Reflective Optical Sensor
4 × TCRT5000
Sensor A1
Sensor A2 (redundant)
Bottom container
Sensor B1
Sensor B2 (redundant)

1 MFRC522 RFID/NFC Module

prototype
Read 4 redundant sensors
Perform redundancy checks
Control 2 servos
Maintain capsule count
Communicate with:
MQTT broker
WWW server
NFC ESP32 (Board B)

Pin Assignment
// Sensors
#define SENSOR_TOP_A     34
#define SENSOR_TOP_B     35
#define SENSOR_BOTTOM_A  32
#define SENSOR_BOTTOM_B  33

// Servos
#define SERVO_DISPENSE   18
#define SERVO_LOCK       19
