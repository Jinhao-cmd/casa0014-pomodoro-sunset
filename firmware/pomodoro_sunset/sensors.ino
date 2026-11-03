// sensors.ino - rotary encoder, button, potentiometer, LDR phone dock and HC-SR04

// ---------- Rotary encoder (read by interrupts, so no click is missed) ----------
volatile int     encoderSteps = 0;   // raw steps, changed inside the interrupt
volatile uint8_t encoderBits  = 0;   // the last two CLK/DT readings
int encoderStepsUsed = 0;            // steps already turned into clicks

// ---------- Button ----------
const unsigned long DEBOUNCE_MS   = 30;
const unsigned long LONG_PRESS_MS = 1500;
bool buttonStable   = false;         // true = pressed, after debouncing
bool buttonLastRaw  = false;
bool longPressFired = false;
unsigned long buttonChangedMs = 0;
unsigned long buttonPressedMs = 0;

// ---------- Slower sensors ----------
unsigned long lastAnalogMs = 0;
unsigned long lastSonarMs  = 0;
unsigned long phoneChangeMs = 0;     // when the dock reading started to look different
const unsigned long PHONE_DEBOUNCE_MS = 800;
int sonarHistory[5] = {0, 0, 0, 0, 0};
int sonarIndex = 0;


// Called on every change of CLK or DT. The table turns each pair of old/new readings
// into +1 (one way), -1 (the other way) or 0 (contact bounce - ignored).
void encoderISR() {
  static const int8_t table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  encoderBits = ((encoderBits << 2) | (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT)) & 0x0F;
  encoderSteps += table[encoderBits];
}

void setupSensors() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);  // pressed = LOW
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  encoderBits = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT),  encoderISR, CHANGE);
}

// Measures the room's light with the dock empty, so a covered LDR can be recognised later
void calibrateLight() {
  lcdShow("Keep dock empty", "Measuring light");
  long total = 0;
  for (int i = 0; i < 20; i++) {
    total += analogRead(PIN_LDR);
    delay(50);
  }
  ambientLight = total / 20.0;
  lightLevel = (int)ambientLight;
  Serial.print("Ambient light level: ");
  Serial.println(ambientLight);
}

void updateSensors(unsigned long now) {
  readEncoder();
  readButton(now);
  if (now - lastAnalogMs >= 100) {     // 10 times per second is plenty
    lastAnalogMs = now;
    readPotentiometer();
    readLightDock(now);
  }
  if (now - lastSonarMs >= 200) {      // 5 times per second
    lastSonarMs = now;
    readDistance(now);
  }
}

// Turns raw encoder steps into whole clicks
void readEncoder() {
  noInterrupts();                      // copy the value safely while it cannot change
  int steps = encoderSteps;
  interrupts();

  int clicks = (steps - encoderStepsUsed) / ENCODER_STEPS_PER_CLICK;
  if (clicks != 0) {
    encoderStepsUsed += clicks * ENCODER_STEPS_PER_CLICK;
    onEncoderTurn(clicks * ENCODER_DIRECTION);
  }
}

// Debounces the encoder button and tells short presses from long presses
void readButton(unsigned long now) {
  bool raw = (digitalRead(PIN_ENC_SW) == LOW);
  if (raw != buttonLastRaw) {
    buttonLastRaw = raw;
    buttonChangedMs = now;
  }
  if (now - buttonChangedMs < DEBOUNCE_MS) {
    return;                            // still bouncing
  }

  if (raw != buttonStable) {
    buttonStable = raw;
    if (buttonStable) {                // just pressed
      buttonPressedMs = now;
      longPressFired = false;
    } else if (!longPressFired) {      // released before it became a long press
      onShortPress();
    }
  }

  if (buttonStable && !longPressFired && now - buttonPressedMs >= LONG_PRESS_MS) {
    longPressFired = true;             // fires while still held, so you feel the reset happen
    onLongPress();
  }
}

void readPotentiometer() {
  int raw = analogRead(PIN_POT);                  // 0 - 1023
  float target = 0.05 + 0.95 * (raw / 1023.0);    // never completely off
  brightness = brightness * 0.7 + target * 0.3;   // smooth out small jitters
}

// The LDR sits under the phone. With VCC -> LDR -> A2 -> 10k -> GND,
// less light gives a LOWER reading, so a covered sensor reads low.
void readLightDock(unsigned long now) {
  lightLevel = analogRead(PIN_LDR);

  // Two thresholds (hysteresis) stop the result flickering near the limit
  bool looksDocked;
  if (phoneDocked) {
    looksDocked = lightLevel < ambientLight * PHONE_OFF_RATIO;
  } else {
    looksDocked = lightLevel < ambientLight * PHONE_ON_RATIO;
  }

  // Accept a change only if it lasts - a hand passing over the dock is ignored
  if (looksDocked != phoneDocked) {
    if (phoneChangeMs == 0) {
      phoneChangeMs = now;
    }
    if (now - phoneChangeMs >= PHONE_DEBOUNCE_MS) {
      phoneDocked = looksDocked;
      phoneChangeMs = 0;
    }
  } else {
    phoneChangeMs = 0;
  }

  // Follow slow changes in the room (daylight, lamps) while the dock is empty
  if (!phoneDocked) {
    ambientLight = ambientLight * 0.99 + lightLevel * 0.01;
  }
}

// Away delay - shortened in demo mode
unsigned long awayDelayMs() {
  return DEMO_MODE ? 10000UL : 60000UL;
}

// Middle value of five readings - removes the occasional wild reading
int median5(const int values[5]) {
  int s[5];
  for (int i = 0; i < 5; i++) {
    s[i] = values[i];
  }
  for (int i = 1; i < 5; i++) {        // insertion sort
    int v = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > v) {
      s[j + 1] = s[j];
      j--;
    }
    s[j + 1] = v;
  }
  return s[2];
}

void readDistance(unsigned long now) {
  // A 10 microsecond pulse on TRIG starts one measurement
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  // ECHO stays HIGH for as long as the sound takes to travel there and back
  unsigned long echoUs = pulseIn(PIN_ECHO, HIGH, 25000UL);   // give up after 25 ms (~4 m)
  int cm = (echoUs == 0) ? 0 : (int)(echoUs / 58);           // 58 us per cm, there and back

  sonarHistory[sonarIndex] = cm;
  sonarIndex = (sonarIndex + 1) % 5;
  distanceCm = median5(sonarHistory);

  bool seenNow = (distanceCm > 0 && distanceCm < PRESENCE_MAX_CM);
  if (seenNow) {
    lastPresentMs = now;
  }
  userPresent = (now - lastPresentMs) < awayDelayMs();       // away only after a while
}
