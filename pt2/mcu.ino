// Sensors
#define SENSOR_TOP_A     34
#define SENSOR_TOP_B     35
#define SENSOR_BOTTOM_A  32
#define SENSOR_BOTTOM_B  33

// Servos
#define SERVO_DISPENSE   18
#define SERVO_LOCK       19

#include <ESP32Servo.h>

Servo dispenseServo;
Servo lockServo;

int capsuleCount = 10;

void setup() {
  dispenseServo.attach(SERVO_DISPENSE);
  lockServo.attach(SERVO_LOCK);

  pinMode(SENSOR_TOP_A, INPUT);
  pinMode(SENSOR_TOP_B, INPUT);
  pinMode(SENSOR_BOTTOM_A, INPUT);
  pinMode(SENSOR_BOTTOM_B, INPUT);
}

//TODO: create specific server in c that checks the data
bool redundancyCheck(int a, int b) {
  return a == b;
}

void dispenseCapsule() {
  dispenseServo.write(90);   // open
  delay(500);
  dispenseServo.write(0);    // close
}

void loop() {
  int topA = digitalRead(SENSOR_TOP_A);
  int topB = digitalRead(SENSOR_TOP_B);

  if (!redundancyCheck(topA, topB)) {
    // safety fault
    return;
  }

  if (capsuleCount > 0) {
    dispenseCapsule();
    capsuleCount--;
  }
}
