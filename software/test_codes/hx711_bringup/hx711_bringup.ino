// HX711 bring-up: is the HX711 seeing the load cell at all?
//
// Pins match the main firmware (esp32_mainv2/config.h): DT 16, SCK 17.
// The old test_codes sketches (loadcell_raw, loadcell_approx) use DT 17 / SCK 16,
// i.e. SWAPPED, so they can't read this board.
//
// Every ~3 s it prints one line per input:
//   A128  channel A, gain 128 (what the firmware uses)  -> A+ / A- pads
//   A64   channel A, gain 64                            -> A+ / A- pads
//   B32   channel B, gain 32                            -> B+ / B- pads
// with the mean raw count, the spread, and the bridge voltage that count means
// (E+ = 4.27 V measured). Hang the box / press the cell and watch which line moves.
#include "HX711.h"

#define DT_PIN   16
#define SCK_PIN  17
#define EXC_V    4.27f    // red-black (E+ / E-) measured on the board

HX711 hx;

// Bridge voltage for a raw count: full scale is +-(EXC/2)/gain over +-2^23 counts
static float microvolts(float counts, uint8_t gain) {
  return counts * (0.5f * EXC_V / gain) / 8388608.0f * 1e6f;
}

static void pinCheck() {
  // DT goes LOW when a conversion is ready (10 per second). Never LOW = no HX711 on
  // this pin / no power; always LOW with readings of 0 = stuck.
  // SCK must be held LOW first: SCK high for > 60 us powers the HX711 down (DT stays high)
  pinMode(SCK_PIN, OUTPUT);
  digitalWrite(SCK_PIN, LOW);
  delay(100);
  pinMode(DT_PIN, INPUT);
  uint32_t lows = 0, highs = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < 1000) { digitalRead(DT_PIN) ? highs++ : lows++; }
  Serial.printf("DT pin %d over 1 s: %lu low / %lu high samples -> %s\n", DT_PIN,
                (unsigned long)lows, (unsigned long)highs,
                lows == 0 ? "NEVER READY: wrong pin, no power, or DT/SCK swapped"
                : highs == 0 ? "always low: check SCK wire" : "OK (toggling)");
}

static void measure(uint8_t gain, const char* name) {
  hx.set_gain(gain, true);
  hx.read(); hx.read();            // the new gain applies from the next conversion
  float sum = 0, mn = 1e9, mx = -1e9;
  const int N = 10;
  for (int i = 0; i < N; i++) {
    const float v = hx.read();
    sum += v; mn = min(mn, v); mx = max(mx, v);
  }
  const float mean = sum / N;
  const char* flag = (fabsf(mean) > 8380000) ? "  <- at the RAIL (input open / overrange)" : "";
  Serial.printf("%-4s mean %9.0f  span %7.0f  = %8.1f uV%s\n", name, mean, mx - mn,
                microvolts(mean, gain), flag);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== HX711 bring-up ===");
  pinCheck();
  hx.begin(DT_PIN, SCK_PIN);
  Serial.println("Expected with a working 1.07 kg box (firmware scale 459518 /kg):");
  Serial.println("  A128 moves by ~490000 counts (~1000 uV) when the box hangs.");
  Serial.println("  Old HX711 saw this cell's empty zero at 574841 counts (~1140 uV).\n");
}

void loop() {
  measure(HX711_CHANNEL_A_GAIN_128, "A128");
  measure(HX711_CHANNEL_A_GAIN_64,  "A64");
  measure(HX711_CHANNEL_B_GAIN_32,  "B32");
  Serial.println();
  delay(1000);
}
