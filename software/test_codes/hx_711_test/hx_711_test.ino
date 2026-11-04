#include "HX711.h"

#define DT 16
#define SCK 17

HX711 scale;

void setup() {
  Serial.begin(115200);

  scale.begin(DT, SCK);

  Serial.println("HX711 test");
}

void loop() {
  if (scale.is_ready()) {
    long value = scale.read();
    Serial.println(value);
  } else {
    Serial.println("NOT READY");
  }

  delay(500);
}