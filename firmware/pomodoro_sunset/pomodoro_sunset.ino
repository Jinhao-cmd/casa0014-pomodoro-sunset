/*
  Pomodoro Sunset - a focus timer that turns Vespera into a setting sun
  CASA0014 - Prototyping the Internet of Things

  Every focus session is a "day" on Vespera: the 72 pixels go out one by one as the
  time runs down, a sunset sweeps across the light when you finish, and an evening star
  appears for every completed pomodoro. Four stars earn a long break.

  Hardware (Arduino MKR WiFi 1010):
    Rotary encoder + push button  - set the focus time, start / pause / reset
    Potentiometer                 - brightness of Vespera and the LCD backlight
    LDR (light sensor)            - "phone dock": the timer only runs while your phone
                                    lies face down on the dock
    HC-SR04 ultrasonic sensor     - pauses the timer when you leave your desk
    Waveshare LCD1602 RGB         - exact time on your desk; backlight follows the state
    Vespera (via MQTT)            - the public, glanceable view for the whole lab

  Sketch files (the Arduino IDE joins all .ino tabs into one program, this one first):
    pomodoro_sunset.ino  - settings, setup(), loop() and the timer state machine
    lcd_display.ino      - a small driver for the LCD1602 RGB (no extra library needed)
    network.ino          - WiFi, MQTT and the JSON status messages
    sensors.ino          - encoder, button, potentiometer, LDR and ultrasonic sensor
    vespera_lights.ino   - draws each frame for Vespera and publishes it
    arduino_secrets.h    - WiFi / MQTT passwords (listed in .gitignore - never upload it)
*/

#include <SPI.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

// =====================================================================
//  SETTINGS - check these before the first upload
// =====================================================================

// ---- Your details ----
String LIGHT_ID = "29";          // your Vespera number from the Google Sheet
String mqtt_topic = "student/CASA0014/luminaire/" + LIGHT_ID;            

// ---- MQTT broker: check both values against the official Vespera workshop sketch ----
const char* MQTT_SERVER = "mqtt.cetools.org";
const int   MQTT_PORT   = 1884;

// ---- Demo mode: one "minute" lasts 4 seconds, so a full cycle fits into the crit ----
const bool DEMO_MODE = true;         // set to false for real 25-minute pomodoros

// ---- Features you can switch off while testing ----
const bool REQUIRE_PHONE_DOCK = false;   // the timer only runs while the LDR is covered
const bool USE_PRESENCE       = false;   // pause when the ultrasonic sensor sees nobody
const bool GENTLE_ANIMATIONS  = true;   // breathing / pulsing effects (more MQTT messages)

// ---- Timer ----
const int FOCUS_MIN_DEFAULT        = 25;
const int FOCUS_MIN_LIMIT          = 5;
const int FOCUS_MAX_LIMIT          = 60;
const int FOCUS_STEP_MIN           = 5;    // minutes added or removed per encoder click
const int SHORT_BREAK_MIN          = 5;
const int LONG_BREAK_MIN           = 15;
const int POMODOROS_PER_LONG_BREAK = 4;

// ---- Sensor tuning (use the Serial Plotter to choose these - see the notes) ----
const float PHONE_ON_RATIO      = 0.40;  // docked when light falls below 40% of the room level
const float PHONE_OFF_RATIO     = 0.60;  // removed when light rises above 60% (hysteresis)
const int   PRESENCE_MAX_CM     = 100;   // someone is "at the desk" when closer than this
const int   ENCODER_STEPS_PER_CLICK = 4; // 4 suits most encoders; use 2 if you need two clicks per step
const int   ENCODER_DIRECTION   = 1;     // use -1 if turning clockwise lowers the time

// ---- Pins, as printed on the MKR1010 ----
const int PIN_ENC_CLK = 4;
const int PIN_ENC_DT  = 5;
const int PIN_ENC_SW  = 3;
const int PIN_POT     = A1;
const int PIN_LDR     = A2;
const int PIN_TRIG    = 7;
const int PIN_ECHO    = 8;     // through the 1k + 2k voltage divider - never connect directly!
// The LCD uses SDA (pin 11) and SCL (pin 12)

// =====================================================================
//  STATE MACHINE
// =====================================================================

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
unsigned long stateStartMs = 0;        // when the current state began (used by animations)

int focusMinutes = FOCUS_MIN_DEFAULT;
unsigned long phaseTotalMs     = 0;    // length of the current focus or break
unsigned long phaseRemainingMs = 0;    // time left in the current focus or break
unsigned long lastTickMs       = 0;

int cycleStars         = 0;   // pomodoros completed in this cycle of four, drawn as stars
int completedTotal     = 0;   // pomodoros completed since the board was switched on
int interruptions      = 0;   // phone lifts during the current session
int interruptionsTotal = 0;   // phone lifts since the board was switched on

const unsigned long SUNSET_MS = 6000;
const unsigned long DAWN_MS   = 3000;

// Sensor results - updated by sensors.ino
float brightness   = 1.0;     // 0.05 - 1.0, from the potentiometer
int   lightLevel   = 0;       // raw LDR reading, 0 - 1023
float ambientLight = 0;       // the room's light level, followed slowly while the dock is empty
bool  phoneDocked  = false;
int   distanceCm   = 0;       // 0 means no echo
bool  userPresent  = true;
unsigned long lastPresentMs = 0;

unsigned long lastDebugMs = 0;
unsigned long loopNowMs   = 0;     // the time at the start of this loop - every timestamp in
                                   // one loop uses it, so time differences never go negative

// Length of one "minute" - shortened in demo mode
unsigned long msPerMinute() {
  return DEMO_MODE ? 4000UL : 60000UL;
}


void setup() {
  Serial.begin(9600);
  delay(1500);                     // time to open the Serial Monitor (no while(!Serial) -
                                   // the board must also run without a computer)
  randomSeed(analogRead(A3));      // an unconnected pin gives a different seed each time

  setupSensors();
  lcdBegin();
  lcdBacklight(255, 160, 60);
  lcdShow("Pomodoro Sunset", "Starting...");

  calibrateLight();                // keep the phone dock empty for a second
  lastPresentMs = millis();

  connectNetwork(20000);           // try for up to 20 s, then carry on offline

  loopNowMs = millis();
  enterState(SETUP_TIME);
}


void loop() {
  unsigned long now = millis();
  loopNowMs = now;

  maintainNetwork(now);      // reconnect if needed and keep MQTT alive
  updateSensors(now);        // encoder, button, potentiometer, LDR, ultrasonic
  updateTimer(now);          // count down and move between states
  updateLights(now);         // draw and publish a Vespera frame (max 5 per second)
  updateLcd(now);            // refresh the LCD text and backlight colour
  publishStatusIfDue(now);   // JSON status for MQTT Explorer / Telegraf
  printDebugIfDue(now);      // sensor values for the Serial Monitor / Plotter
}


// Moves to a new state and does the set-up that state needs
void enterState(State next) {
  State previous = state;
  state = next;
  stateStartMs = loopNowMs;

  switch (next) {
    case SETUP_TIME:
      interruptions = 0;
      break;
    case FOCUS:
      if (previous == SETUP_TIME) {        // a brand new session, not a resume
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
  forceLightUpdate();      // show the new state on Vespera straight away
  publishStatus();         // and tell the status topic
}


// Counts down the current phase and handles automatic state changes
void updateTimer(unsigned long now) {
  unsigned long elapsed = now - lastTickMs;
  lastTickMs = now;

  // Only these states count down; pauses and animations freeze the time
  if (state == FOCUS || state == SHORT_BREAK || state == LONG_BREAK) {
    phaseRemainingMs = (elapsed >= phaseRemainingMs) ? 0 : phaseRemainingMs - elapsed;
  }

  // Automatic pause / resume during focus: the phone has priority over presence
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

  // End of each phase
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
      cycleStars = 0;          // a new cycle of four begins
    }
    enterState(DAWN);
  } else if (state == DAWN && now - stateStartMs >= DAWN_MS) {
    enterState(SETUP_TIME);
  }
}


// ---------- What the controls do (called from sensors.ino) ----------

void onEncoderTurn(int clicks) {
  if (state != SETUP_TIME) {
    return;                    // the time can only be changed before starting
  }
  focusMinutes = constrain(focusMinutes + clicks * FOCUS_STEP_MIN, FOCUS_MIN_LIMIT, FOCUS_MAX_LIMIT);
  forceLightUpdate();          // update the preview on Vespera
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
      enterState(FOCUS);       // resumes where it stopped
      break;
    case SHORT_BREAK:
    case LONG_BREAK:           // skip the rest of the break
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
  if (state != SETUP_TIME) {   // cancel and go back to choosing the time
    showLcdMessage("Reset", "", 1000);
    enterState(SETUP_TIME);
  }
}


// Prints "name:value" pairs, which the Serial Plotter can draw as lines
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
