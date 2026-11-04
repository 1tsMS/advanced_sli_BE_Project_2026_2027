#include "HX711.h"

#define DT  16
#define SCK 17

HX711 scale;

// TEMPORARY calibration
// Based on your hand-pressure test.
// Replace this after getting a known weight.
float COUNTS_PER_KG = 122000.0;

long zero = 0;

void setup() {

  Serial.begin(115200);

  scale.begin(DT, SCK);

  Serial.println();
  Serial.println("==============================");
  Serial.println("   5 KG LOAD CELL TEST");
  Serial.println("==============================");

  delay(2000);

  // -----------------------------
  // Automatic tare
  // -----------------------------

  Serial.println("Remove all load...");
  delay(1000);

  long sum = 0;
  int samples = 0;

  while (samples < 30) {

    if (scale.is_ready()) {

      sum += scale.read();
      samples++;

      delay(50);
    }
  }

  zero = sum / 30;

  Serial.print("ZERO = ");
  Serial.println(zero);

  Serial.println("Ready!");
  Serial.println();
}

void loop() {

  if (!scale.is_ready()) {
    Serial.println("HX711 NOT READY");
    delay(200);
    return;
  }

  // Average 5 readings
  long sum = 0;

  for (int i = 0; i < 5; i++) {

    while (!scale.is_ready()) {
      delay(1);
    }

    sum += scale.read();
  }

  float raw = sum / 5.0;

  float delta = raw - zero;

  // Convert counts → kg
  float weight = delta / COUNTS_PER_KG;

  // Zero deadband
  if (weight > -0.02 && weight < 0.02) {
    weight = 0;
  }

  // Don't show negative weight
  if (weight < 0) {
    weight = 0;
  }

  Serial.print("Raw: ");
  Serial.print(raw, 0);

  Serial.print(" | Delta: ");
  Serial.print(delta, 0);

  Serial.print(" | Weight: ");
  Serial.print(weight, 3);

  Serial.println(" kg");

  delay(200);
}