#define IN1 13
#define IN2 14
#define STBY 12   // DRV8833 nSLEEP (EEP/STBY on the board): LOW or floating = driver asleep

#define ENC_A 18
#define ENC_B 19

volatile long encoderCount = 0;

void IRAM_ATTR encoderISR() {
  int A = digitalRead(ENC_A);
  int B = digitalRead(ENC_B);

  if (A == B)
    encoderCount++;
  else
    encoderCount--;
}

void setup() {
  Serial.begin(115200);

  // Motor driver
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, HIGH);   // wake the driver (same as the main firmware)

  // Encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);

  // Motor stopped initially
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  Serial.println("N20 MOTOR + ENCODER TEST");
  Serial.println("Starting in 2 seconds...");
  delay(2000);
}

void loop() {

  // Reset count
  noInterrupts();
  encoderCount = 0;
  interrupts();

  Serial.println("FORWARD");

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  delay(3000);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  delay(1000);

  noInterrupts();
  long forwardCount = encoderCount;
  interrupts();

  Serial.print("Forward encoder count: ");
  Serial.println(forwardCount);


  // Reset count
  noInterrupts();
  encoderCount = 0;
  interrupts();

  Serial.println("REVERSE");

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  delay(3000);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  delay(1000);

  noInterrupts();
  long reverseCount = encoderCount;
  interrupts();

  Serial.print("Reverse encoder count: ");
  Serial.println(reverseCount);

  Serial.println("------------------------");

  delay(2000);
}