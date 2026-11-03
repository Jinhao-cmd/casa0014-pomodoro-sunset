/*
 * Pomodoro Sunset v2
 * CASA0014 - Prototyping the Internet of Things
 *
 * Hardware: MKR WiFi 1010, rotary encoder, potentiometer,
 * LDR, HC-SR04, LCD1602 RGB and Vespera.
 *
 * Vespera is controlled through MQTT.
 * Each frame contains RGB data for 72 pixels (216 bytes).
 *
 * MQTT topic: student/CASA0014/luminaire/<LIGHT_ID>
 */

#include <SPI.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

// =====================================================================
//  SETTINGS
// =====================================================================

// Vespera ID
const char* LIGHT_ID = "29";

// MQTT
const char* MQTT_SERVER = "mqtt.cetools.org";
const int   MQTT_PORT   = 1884;

// demo: 1 min = 4 s
const bool DEMO_MODE = true;            

// feature switches
const bool REQUIRE_PHONE_DOCK = true;   // need phone on dock
const bool USE_PRESENCE       = true;   // pause when away
const bool GENTLE_ANIMATIONS  = true;   

// Timer
const int FOCUS_MIN_DEFAULT        = 25;
const int FOCUS_MIN_LIMIT          = 5;
const int FOCUS_MAX_LIMIT          = 60;
const int FOCUS_STEP_MIN           = 5;     // minutes added or removed per encoder click
const int SHORT_BREAK_MIN          = 5;
const int LONG_BREAK_MIN           = 15;
const int POMODOROS_PER_LONG_BREAK = 4;

// thresholds
const float PHONE_ON_RATIO          = 0.40; // docked below 40% of ambient
const float PHONE_OFF_RATIO         = 0.60; // hysteresis
const int   PRESENCE_MAX_CM         = 100;  // cm
const int   ENCODER_STEPS_PER_CLICK = 4;    // 2 or 4
const int   ENCODER_DIRECTION       = 1;    

// star pixels
const int PIXEL_ORDER[72] = {
   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,
  12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
  24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
  36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
  48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
  60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71
};
// Positions (in the order above) where the evening stars appear
const int STAR_PIXELS[4] = {0, 18, 36, 54};

// pins
const int PIN_ENC_CLK = 4;
const int PIN_ENC_DT  = 5;
const int PIN_ENC_SW  = 3;
const int PIN_POT     = A1;
const int PIN_LDR     = A2;
const int PIN_TRIG    = 7;
const int PIN_ECHO    = 8;   // via 1k/2k divider
// LCD on SDA/SCL

// =====================================================================
//  GLOBAL VARIABLES
// =====================================================================

// State machine
enum State {
  SETUP_TIME,      // choosing the focus length
  FOCUS,           // timer running
  PAUSED_MANUAL,   // paused with the button
  PAUSED_PHONE,    // phone lifted from the dock
  PAUSED_AWAY,     // nobody at the desk
  SUNSET,          // end-of-focus animation
  SHORT_BREAK,
  LONG_BREAK,
  DAWN             // end-of-break animation
};
const char* STATE_NAMES[] = {"setup", "focus", "paused", "phone_lifted", "away",
                             "sunset", "short_break", "long_break", "dawn"};

State state = SETUP_TIME;
unsigned long stateStartMs = 0;
unsigned long loopNowMs    = 0;     

int focusMinutes = FOCUS_MIN_DEFAULT;
unsigned long phaseTotalMs     = 0; 
unsigned long phaseRemainingMs = 0; 
unsigned long lastTickMs       = 0;

int cycleStars         = 0;         // 0-4
int completedTotal     = 0;
int interruptions      = 0;         
int interruptionsTotal = 0;

const unsigned long SUNSET_MS = 6000;
const unsigned long DAWN_MS   = 3000;

// Sensor results
float brightness   = 1.0;           // 0.05-1.0
int   lightLevel   = 0;             // raw LDR reading 0 - 1023
float ambientLight = 0;             
bool  phoneDocked  = false;
int   distanceCm   = 0;             // 0 = no echo
bool  userPresent  = true;
unsigned long lastPresentMs = 0;

// Encoder and button 
volatile int     encoderSteps = 0;  // ISR
volatile uint8_t encoderBits  = 0;
int encoderStepsUsed = 0;

const unsigned long DEBOUNCE_MS   = 30;
const unsigned long LONG_PRESS_MS = 1500;
bool buttonStable   = false;
bool buttonLastRaw  = false;
bool longPressFired = false;
unsigned long buttonChangedMs = 0;
unsigned long buttonPressedMs = 0;

// Slower sensors 
unsigned long lastAnalogMs  = 0;
unsigned long lastSonarMs   = 0;
unsigned long phoneChangeMs = 0;
const unsigned long PHONE_DEBOUNCE_MS = 800;
int sonarHistory[5] = {0, 0, 0, 0, 0};
int sonarIndex = 0;

// LCD
const uint8_t LCD_ADDR = 0x3E;
const uint8_t RGB_CANDIDATES[] = {0x60, 0x62};
uint8_t rgbAddr = 0;               
char lcdLines[2][17] = {"", ""};
char messageLines[2][17];
unsigned long messageUntilMs = 0;
unsigned long lastLcdMs = 0;
int lastBacklight[3] = {-1, -1, -1};

// ---- Vespera frames ----
const int NUM_LEDS = 72;
byte frame[NUM_LEDS * 3];           // R,G,B for each pixel
byte lastSent[NUM_LEDS * 3];
unsigned long lastFrameMs   = 0;
unsigned long lastPublishMs = 0;
bool forceSend = true;
const unsigned long FRAME_INTERVAL_MS = 200;    
const unsigned long RESEND_EVERY_MS   = 30000;  

const int SUNSET_STOPS = 4;         // gold -> orange -> magenta -> deep blue
const float SUNSET_COLOURS[SUNSET_STOPS][3] = {
  {255, 150, 40}, {255, 70, 10}, {170, 30, 110}, {10, 20, 90}
};

// Network
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
char vesperaTopic[64];
unsigned long lastNetworkAttemptMs = 0;
const unsigned long NETWORK_RETRY_MS = 10000;

unsigned long lastDebugMs = 0;


// =====================================================================
//  SETUP AND LOOP
// =====================================================================

void setup() {
  Serial.begin(9600);
  delay(1500);                     
  randomSeed(analogRead(A3));

  setupSensors();
  lcdBegin();
  lcdBacklight(255, 160, 60);
  lcdShow("Pomodoro Sunset", "Starting...");

  calibrateLight();                
  lastPresentMs = millis();

  connectNetwork(20000);           // 20 s timeout

  loopNowMs = millis();
  enterState(SETUP_TIME);
}

void loop() {
  unsigned long now = millis();
  loopNowMs = now;

  maintainNetwork(now);      
  updateSensors(now);        
  updateTimer(now);         
  updateLights(now);        
  updateLcd(now);            
  printDebugIfDue(now);      
}


// =====================================================================
//  STATE MACHINE
// =====================================================================

unsigned long msPerMinute() {
  return DEMO_MODE ? 4000UL : 60000UL;
}

void enterState(State next) {
  State previous = state;
  state = next;
  stateStartMs = loopNowMs;

  switch (next) {
    case SETUP_TIME:
      interruptions = 0;
      break;
    case FOCUS:
      if (previous == SETUP_TIME) {        // new session
        phaseTotalMs = (unsigned long)focusMinutes * msPerMinute();
        phaseRemainingMs = phaseTotalMs;
      }
      break;
    case SHORT_BREAK:
      phaseTotalMs = (unsigned long)SHORT_BREAK_MIN * msPerMinute();
      phaseRemainingMs = phaseTotalMs;
      break;
    case LONG_BREAK:
      phaseTotalMs = (unsigned long)LONG_BREAK_MIN * msPerMinute();
      phaseRemainingMs = phaseTotalMs;
      break;
    default:
      break;
  }

  lastTickMs = loopNowMs;
  Serial.print("State -> ");
  Serial.println(STATE_NAMES[next]);
  forceLightUpdate();                      
}

void updateTimer(unsigned long now) {
  unsigned long elapsed = now - lastTickMs;
  lastTickMs = now;

  // count down
  if (state == FOCUS || state == SHORT_BREAK || state == LONG_BREAK) {
    phaseRemainingMs = (elapsed >= phaseRemainingMs) ? 0 : phaseRemainingMs - elapsed;
  }

  // auto pause
  if (state == FOCUS || state == PAUSED_PHONE || state == PAUSED_AWAY) {
    State target = FOCUS;
    if (REQUIRE_PHONE_DOCK && !phoneDocked) {
      target = PAUSED_PHONE;
    } else if (USE_PRESENCE && !userPresent) {
      target = PAUSED_AWAY;
    }
    if (target != state) {
      if (target == PAUSED_PHONE && state == FOCUS) {
        interruptions++;
        interruptionsTotal++;
      }
      enterState(target);
    }
  }

  if (state == FOCUS && phaseRemainingMs == 0) {
    enterState(SUNSET);
  } else if (state == SUNSET && now - stateStartMs >= SUNSET_MS) {
    completedTotal++;
    cycleStars++;
    if (cycleStars >= POMODOROS_PER_LONG_BREAK) {
      enterState(LONG_BREAK);
    } else {
      enterState(SHORT_BREAK);
    }
  } else if ((state == SHORT_BREAK || state == LONG_BREAK) && phaseRemainingMs == 0) {
    if (state == LONG_BREAK) {
      cycleStars = 0;                      
    }
    enterState(DAWN);
  } else if (state == DAWN && now - stateStartMs >= DAWN_MS) {
    enterState(SETUP_TIME);
  }
}

// controls

void onEncoderTurn(int clicks) {
  if (state != SETUP_TIME) {
    return;                                // the time can only be changed before starting
  }
  focusMinutes = constrain(focusMinutes + clicks * FOCUS_STEP_MIN, FOCUS_MIN_LIMIT, FOCUS_MAX_LIMIT);
  forceLightUpdate();                   
}

void onShortPress() {
  switch (state) {
    case SETUP_TIME:
      if (REQUIRE_PHONE_DOCK && !phoneDocked) {
        showLcdMessage("Dock phone first", "then press again", 2000);
      } else {
        enterState(FOCUS);
      }
      break;
    case FOCUS:
      enterState(PAUSED_MANUAL);
      break;
    case PAUSED_MANUAL:
      enterState(FOCUS);                   
      break;
    case SHORT_BREAK:
    case LONG_BREAK:                       // skip break
      if (state == LONG_BREAK) {
        cycleStars = 0;
      }
      enterState(DAWN);
      break;
    default:
      break;
  }
}

void onLongPress() {
  if (state != SETUP_TIME) {               // cancel and go back to choosing the time
    showLcdMessage("Reset", "", 1000);
    enterState(SETUP_TIME);
  }
}


// =====================================================================
//  SENSORS
// =====================================================================

// encoder ISR
void encoderISR() {
  static const int8_t table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  encoderBits = ((encoderBits << 2) | (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT)) & 0x0F;
  encoderSteps += table[encoderBits];
}

void setupSensors() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);      // pressed = LOW
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  encoderBits = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT),  encoderISR, CHANGE);
}

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
  if (now - lastAnalogMs >= 100) {         
    lastAnalogMs = now;
    readPotentiometer();
    readLightDock(now);
  }
  if (now - lastSonarMs >= 200) {          
    lastSonarMs = now;
    readDistance(now);
  }
}

void readEncoder() {
  noInterrupts();                         
  int steps = encoderSteps;
  interrupts();

  int clicks = (steps - encoderStepsUsed) / ENCODER_STEPS_PER_CLICK;
  if (clicks != 0) {
    encoderStepsUsed += clicks * ENCODER_STEPS_PER_CLICK;
    onEncoderTurn(clicks * ENCODER_DIRECTION);
  }
}

void readButton(unsigned long now) {
  bool raw = (digitalRead(PIN_ENC_SW) == LOW);
  if (raw != buttonLastRaw) {
    buttonLastRaw = raw;
    buttonChangedMs = now;
  }
  if (now - buttonChangedMs < DEBOUNCE_MS) {
    return;                                
  }

  if (raw != buttonStable) {
    buttonStable = raw;
    if (buttonStable) {                    
      buttonPressedMs = now;
      longPressFired = false;
    } else if (!longPressFired) {         
      onShortPress();
    }
  }

  if (buttonStable && !longPressFired && now - buttonPressedMs >= LONG_PRESS_MS) {
    longPressFired = true;
    onLongPress();
  }
}

void readPotentiometer() {
  int raw = analogRead(PIN_POT);                  // 0 - 1023
  float target = 0.05 + 0.95 * (raw / 1023.0);    // never completely off
  brightness = brightness * 0.7 + target * 0.3;   // smoothing
}

// darker = lower
void readLightDock(unsigned long now) {
  lightLevel = analogRead(PIN_LDR);

  bool looksDocked;                        
  if (phoneDocked) {
    looksDocked = lightLevel < ambientLight * PHONE_OFF_RATIO;
  } else {
    looksDocked = lightLevel < ambientLight * PHONE_ON_RATIO;
  }

  if (looksDocked != phoneDocked) {        // debounce
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

  if (!phoneDocked) {                      
    ambientLight = ambientLight * 0.99 + lightLevel * 0.01;
  }
}

unsigned long awayDelayMs() {
  return DEMO_MODE ? 10000UL : 60000UL;
}

int median5(const int values[5]) {
  int s[5];
  for (int i = 0; i < 5; i++) {
    s[i] = values[i];
  }
  for (int i = 1; i < 5; i++) {            // insertion sort
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
  digitalWrite(PIN_TRIG, LOW);             
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long echoUs = pulseIn(PIN_ECHO, HIGH, 25000UL);   // ~4 m timeout
  int cm = (echoUs == 0) ? 0 : (int)(echoUs / 58);           // 0 = no echo

  sonarHistory[sonarIndex] = cm;           // median filter
  sonarIndex = (sonarIndex + 1) % 5;
  distanceCm = median5(sonarHistory);

  if (distanceCm > 0 && distanceCm < PRESENCE_MAX_CM) {
    lastPresentMs = now;
  }
  userPresent = (now - lastPresentMs) < awayDelayMs();
}


// =====================================================================
//  LCD1602 RGB 
// =====================================================================

bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void lcdCommand(uint8_t cmd) {
  Wire.beginTransmission(LCD_ADDR);
  Wire.write(0x80);                        // command
  Wire.write(cmd);
  Wire.endTransmission();
}

void lcdWriteRow(uint8_t row, const char* text) {
  lcdCommand(row == 0 ? 0x80 : 0xC0);      
  Wire.beginTransmission(LCD_ADDR);
  Wire.write(0x40);                        // data
  int i = 0;
  for (; i < 16 && text[i] != '\0'; i++) {
    Wire.write(text[i]);
  }
  for (; i < 16; i++) {
    Wire.write(' ');                       // pad
  }
  Wire.endTransmission();
}

void rgbRegister(uint8_t reg, uint8_t value) {
  if (rgbAddr == 0) {
    return;
  }
  Wire.beginTransmission(rgbAddr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void lcdBegin() {
  Wire.begin();
  delay(50);

  Serial.print("I2C devices found:");      
  for (uint8_t a = 1; a < 127; a++) {
    if (i2cDevicePresent(a)) {
      Serial.print(" 0x");
      Serial.print(a, HEX);
    }
  }
  Serial.println();
  if (!i2cDevicePresent(LCD_ADDR)) {
    Serial.println("LCD not found at 0x3E - check VCC, GND, SDA (11) and SCL (12)");
  }

  lcdCommand(0x28); delay(5);              
  lcdCommand(0x28); delay(1);
  lcdCommand(0x28);
  lcdCommand(0x0C);                       
  lcdCommand(0x01); delay(2);              
  lcdCommand(0x06);                        

  for (uint8_t i = 0; i < sizeof(RGB_CANDIDATES); i++) {
    if (i2cDevicePresent(RGB_CANDIDATES[i])) {
      rgbAddr = RGB_CANDIDATES[i];
      break;
    }
  }
  if (rgbAddr == 0) {
    Serial.println("RGB backlight chip not found - text works, colours will not");
  } else {
    rgbRegister(0x00, 0x00);              
    rgbRegister(0x08, 0xFF);               
    rgbRegister(0x01, 0x20);               
  }
}

void lcdBacklight(int r, int g, int b) {
  if (r == lastBacklight[0] && g == lastBacklight[1] && b == lastBacklight[2]) {
    return;
  }
  rgbRegister(0x04, r);                    // red 
  rgbRegister(0x03, g);                    // green
  rgbRegister(0x02, b);                    // blue
  lastBacklight[0] = r;
  lastBacklight[1] = g;
  lastBacklight[2] = b;
}

void lcdShow(const char* line1, const char* line2) {
  lcdWriteRow(0, line1);
  lcdWriteRow(1, line2);
  strncpy(lcdLines[0], line1, 16);
  lcdLines[0][16] = '\0';
  strncpy(lcdLines[1], line2, 16);
  lcdLines[1][16] = '\0';
}

void showLcdMessage(const char* line1, const char* line2, unsigned long durationMs) {
  strncpy(messageLines[0], line1, 16);
  messageLines[0][16] = '\0';
  strncpy(messageLines[1], line2, 16);
  messageLines[1][16] = '\0';
  messageUntilMs = millis() + durationMs;
  lastLcdMs = 0;
}

void buildLcdText(char* line1, char* line2) {
  unsigned long secs = (phaseRemainingMs + 999) / 1000;
  int mm = secs / 60;
  int ss = secs % 60;

  char stars[5];                           
  for (int i = 0; i < 4; i++) {
    stars[i] = (i < cycleStars) ? '*' : '-';
  }
  stars[4] = '\0';

  switch (state) {
    case SETUP_TIME:
      snprintf(line1, 17, "Set focus %2d min", focusMinutes);
      if (REQUIRE_PHONE_DOCK && !phoneDocked) {
        snprintf(line2, 17, "Dock phone %s", stars);
      } else {
        snprintf(line2, 17, "Press start %s", stars);
      }
      break;
    case FOCUS:
      snprintf(line1, 17, "Focus      %02d:%02d", mm, ss);
      snprintf(line2, 17, "%s  Lifted:%d", stars, interruptions);
      break;
    case PAUSED_MANUAL:
      snprintf(line1, 17, "Paused     %02d:%02d", mm, ss);
      snprintf(line2, 17, "Press to resume");
      break;
    case PAUSED_PHONE:
      snprintf(line1, 17, "Phone up!  %02d:%02d", mm, ss);
      snprintf(line2, 17, "Put it back");
      break;
    case PAUSED_AWAY:
      snprintf(line1, 17, "Away       %02d:%02d", mm, ss);
      snprintf(line2, 17, "Paused till back");
      break;
    case SUNSET:
      snprintf(line1, 17, "Focus complete!");
      snprintf(line2, 17, "Sunset...");
      break;
    case SHORT_BREAK:
      snprintf(line1, 17, "Break      %02d:%02d", mm, ss);
      snprintf(line2, 17, "%s  Rest eyes", stars);
      break;
    case LONG_BREAK:
      snprintf(line1, 17, "Long brk   %02d:%02d", mm, ss);
      snprintf(line2, 17, "%s Well done!", stars);
      break;
    case DAWN:
      snprintf(line1, 17, "Break over");
      snprintf(line2, 17, "Ready for more?");
      break;
  }
}

void updateBacklight(unsigned long now) {
  int r, g, b;
  switch (state) {
    case SETUP_TIME:    r = 255; g = 190; b = 120; break;   // soft warm white
    case FOCUS:         r = 255; g = 140; b = 20;  break;   // golden daylight
    case PAUSED_MANUAL:
    case PAUSED_AWAY:   r = 80;  g = 50;  b = 10;  break;   // dimmed
    case PAUSED_PHONE:                                      // blinking orange
      if ((now / 500) % 2 == 0) { r = 255; g = 50; b = 0; }
      else                      { r = 40;  g = 8;  b = 0; }
      break;
    case SUNSET:        r = 200; g = 60;  b = 120; break;   // dusk magenta
    case SHORT_BREAK:   r = 40;  g = 80;  b = 255; break;   // evening blue
    case LONG_BREAK:    r = 90;  g = 60;  b = 255; break;   // night violet
    default:            r = 255; g = 170; b = 80;  break;   // dawn
  }
  float k = max(outputBrightness(), 0.25f);                
  lcdBacklight((int)(r * k), (int)(g * k), (int)(b * k));
}

void updateLcd(unsigned long now) {
  if (now - lastLcdMs < 250) {
    return;
  }
  lastLcdMs = now;

  char line1[17];
  char line2[17];
  if (now < messageUntilMs) {
    strcpy(line1, messageLines[0]);
    strcpy(line2, messageLines[1]);
  } else {
    buildLcdText(line1, line2);
  }

  if (strcmp(line1, lcdLines[0]) != 0) {   
    lcdWriteRow(0, line1);
    strcpy(lcdLines[0], line1);
  }
  if (strcmp(line2, lcdLines[1]) != 0) {
    lcdWriteRow(1, line2);
    strcpy(lcdLines[1], line2);
  }

  updateBacklight(now);
}


// =====================================================================
//  VESPERA LIGHTS
// =====================================================================

// 5% steps
float outputBrightness() {
  float q = round(brightness * 20) / 20.0;
  return max(q, 0.05f);
}

void forceLightUpdate() {
  forceSend = true;
  lastFrameMs = 0;                         
}

void setPixel(int i, float r, float g, float b) {
  if (i < 0 || i >= NUM_LEDS) {
    return;
  }
  int p = PIXEL_ORDER[i];
  if (p < 0 || p >= NUM_LEDS) {
    return;
  }
  float k = outputBrightness();
  frame[p * 3 + 0] = (byte)constrain(r * k, 0, 255);
  frame[p * 3 + 1] = (byte)constrain(g * k, 0, 255);
  frame[p * 3 + 2] = (byte)constrain(b * k, 0, 255);
}

// 0..1..0 wave
float wave(unsigned long now, unsigned long periodMs, float phase) {
  if (!GENTLE_ANIMATIONS) {
    return 1.0;
  }
  float x = (now % periodMs) / (float)periodMs + phase;
  return 0.5 - 0.5 * cos(2 * PI * x);
}

// t: 0 = gold, 1 = blue
void sunsetColour(float t, float* r, float* g, float* b) {
  t = constrain(t, 0, 1);
  float pos = t * (SUNSET_STOPS - 1);
  int i = min((int)pos, SUNSET_STOPS - 2);
  float f = pos - i;
  *r = SUNSET_COLOURS[i][0] + (SUNSET_COLOURS[i + 1][0] - SUNSET_COLOURS[i][0]) * f;
  *g = SUNSET_COLOURS[i][1] + (SUNSET_COLOURS[i + 1][1] - SUNSET_COLOURS[i][1]) * f;
  *b = SUNSET_COLOURS[i][2] + (SUNSET_COLOURS[i + 1][2] - SUNSET_COLOURS[i][2]) * f;
}

void drawStars(unsigned long now, int count) {
  for (int s = 0; s < count && s < 4; s++) {
    float k = 0.6 + 0.4 * wave(now, 1500, s * 0.25);   
    setPixel(STAR_PIXELS[s], 255 * k, 255 * k, 255 * k);
  }
}

void drawFrame(unsigned long now) {
  memset(frame, 0, sizeof(frame));

  float fraction = (phaseTotalMs > 0) ? (float)phaseRemainingMs / phaseTotalMs : 0;
  int lit = (int)ceil(fraction * NUM_LEDS);              

  switch (state) {
    case SETUP_TIME: {
      // Preview
      int preview = (int)round((float)focusMinutes / FOCUS_MAX_LIMIT * NUM_LEDS);
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i < preview) setPixel(i, 255, 150, 40);
        else             setPixel(i, 12, 7, 2);
      }
      break;
    }
    case FOCUS: {
      bool lastMinute = phaseRemainingMs <= msPerMinute();
      for (int i = 0; i < lit; i++) {
        float k = 1.0;
        if (lastMinute) {
          k = 0.4 + 0.6 * wave(now, 1000, 0);          // last minute
        } else if (i == lit - 1) {
          k = 0.3 + 0.7 * wave(now, 2000, 0);          // next
        }
        setPixel(i, 255 * k, 150 * k, 40 * k);
      }
      break;
    }
    case PAUSED_MANUAL:
    case PAUSED_AWAY: {
      float k = 0.1 + 0.2 * wave(now, 3000, 0);        
      for (int i = 0; i < lit; i++) {
        setPixel(i, 255 * k, 150 * k, 40 * k);
      }
      break;
    }
    case PAUSED_PHONE: {
      float k = 0.2 + 0.8 * wave(now, 800, 0);         
      for (int i = 0; i < lit; i++) {
        setPixel(i, 255 * k, 60 * k, 0);
      }
      break;
    }
    case SUNSET: {
      float t = (float)(now - stateStartMs) / SUNSET_MS;
      for (int i = 0; i < NUM_LEDS; i++) {             
        float r, g, b;
        sunsetColour(t * 1.6 - ((float)i / NUM_LEDS) * 0.6, &r, &g, &b);
        setPixel(i, r, g, b);
      }
      if (t > 0.85) {
        drawStars(now, cycleStars + 1);                
      }
      break;
    }
    case SHORT_BREAK:
    case LONG_BREAK: {
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i < lit) setPixel(i, 20, 40, 160);         
        else         setPixel(i, 3, 5, 20);            
      }
      drawStars(now, cycleStars);
      break;
    }
    case DAWN: {
      float t = (float)(now - stateStartMs) / DAWN_MS;
      for (int i = 0; i < NUM_LEDS; i++) {             // reverse sunset
        float r, g, b;
        sunsetColour(1.0 - (t * 1.6 - ((float)i / NUM_LEDS) * 0.6), &r, &g, &b);
        setPixel(i, r, g, b);
      }
      break;
    }
  }
}

// max 5 fps
void updateLights(unsigned long now) {
  if (now - lastFrameMs < FRAME_INTERVAL_MS) {
    return;
  }
  lastFrameMs = now;
  drawFrame(now);

  bool changed   = memcmp(frame, lastSent, sizeof(frame)) != 0;
  bool resendDue = now - lastPublishMs >= RESEND_EVERY_MS;
  if (changed || resendDue || forceSend) {
    if (publishFrame(frame, sizeof(frame))) {
      memcpy(lastSent, frame, sizeof(frame));
      lastPublishMs = now;
      forceSend = false;
    }
  }
}


// =====================================================================
// Network 
// =====================================================================

bool connectMqtt() {
  // client ID from MAC
  byte mac[6];
  WiFi.macAddress(mac);
  char clientId[40];
  snprintf(clientId, sizeof(clientId), "MKR1010_Pomodoro_%02X%02X%02X%02X%02X%02X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);

  Serial.print("Connecting to MQTT as ");
  Serial.print(clientId);
  Serial.print("... ");

  if (mqtt.connect(clientId, SECRET_MQTTUSER, SECRET_MQTTPASS)) {
    Serial.println("connected");
    forceLightUpdate();
    return true;
  }
  Serial.print("failed, state = ");        // -2 = cannot reach broker, 4 = wrong username/password
  Serial.println(mqtt.state());
  return false;
}

void connectNetwork(unsigned long timeoutMs) {
  snprintf(vesperaTopic, sizeof(vesperaTopic), "student/CASA0014/luminaire/%s", LIGHT_ID);

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setBufferSize(512);                 // default is 256
  mqtt.setSocketTimeout(5);               

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found!");
    return;
  }

  lcdShow("Connecting WiFi", SECRET_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    delay(3000);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Vespera topic: ");
    Serial.println(vesperaTopic);
    connectMqtt();
  } else {
    Serial.println("WiFi not connected - running offline and retrying every 10 s");
  }
  lastNetworkAttemptMs = millis();
}

void maintainNetwork(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (now - lastNetworkAttemptMs < NETWORK_RETRY_MS) {
    return;
  }
  lastNetworkAttemptMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost - reconnecting (the timer keeps running)");
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    if (WiFi.status() != WL_CONNECTED) {
      return;
    }
  }
  connectMqtt();
}


bool publishFrame(const byte* data, unsigned int length) {
  if (!mqtt.connected()) {
    return false;
  }
  return mqtt.publish(vesperaTopic, data, length);
}


// =====================================================================
//  DEBUG 
// =====================================================================

void printDebugIfDue(unsigned long now) {
  if (now - lastDebugMs < 500) {
    return;
  }
  lastDebugMs = now;
  Serial.print("light:");       Serial.print(lightLevel);
  Serial.print(",ambient:");    Serial.print((int)ambientLight);
  Serial.print(",dockLine:");   Serial.print((int)(ambientLight * PHONE_ON_RATIO));
  Serial.print(",distance:");   Serial.print(distanceCm);
  Serial.print(",phone:");      Serial.print(phoneDocked ? 100 : 0);
  Serial.print(",present:");    Serial.print(userPresent ? 100 : 0);
  Serial.print(",brightness:"); Serial.println((int)(outputBrightness() * 100));
}
