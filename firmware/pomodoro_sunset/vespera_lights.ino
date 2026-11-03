// vespera_lights.ino - draws each frame for Vespera's 72 pixels and publishes it

const int NUM_LEDS = 72;
byte frame[NUM_LEDS * 3];             // the frame being drawn (R,G,B per pixel)
byte lastSent[NUM_LEDS * 3];          // the last frame sent to Vespera
unsigned long lastFrameMs   = 0;
unsigned long lastPublishMs = 0;
bool forceSend = true;

const unsigned long FRAME_INTERVAL_MS = 200;     // at most 5 frames per second
const unsigned long RESEND_EVERY_MS   = 30000;   // repeat the frame now and then

// Pixels used for the evening stars, one per completed pomodoro.
// Use the pixel finder (workshop sketch, DEMO 2) to pick positions that look good on Vespera.
const int STAR_PIXELS[4] = {0, 18, 36, 54};

// Sunset colours: gold -> orange -> magenta -> deep blue
const int SUNSET_STOPS = 4;
const float SUNSET_COLOURS[SUNSET_STOPS][3] = {
  {255, 150, 40}, {255, 70, 10}, {170, 30, 110}, {10, 20, 90}
};


// Logical position (the order the "sun" sets in) -> Vespera's pixel number.
// Leave as it is, or change it once you know how Vespera's pixels are arranged.
int mapPixel(int i) {
  return i;
}

// Brightness from the potentiometer, rounded to 5% steps so tiny jitters
// do not create new frames
float outputBrightness() {
  float q = round(brightness * 20) / 20.0;
  return max(q, 0.05f);
}

void forceLightUpdate() {
  forceSend = true;
  lastFrameMs = 0;                    // draw on the next loop
}

void setPixel(int i, float r, float g, float b) {
  if (i < 0 || i >= NUM_LEDS) {
    return;
  }
  int p = mapPixel(i);
  float k = outputBrightness();
  frame[p * 3 + 0] = (byte)constrain(r * k, 0, 255);
  frame[p * 3 + 1] = (byte)constrain(g * k, 0, 255);
  frame[p * 3 + 2] = (byte)constrain(b * k, 0, 255);
}

// A smooth 0 -> 1 -> 0 wave, used for breathing and pulsing effects.
// Returns a steady 1 when GENTLE_ANIMATIONS is off, so frames only change when needed.
float wave(unsigned long now, unsigned long periodMs, float phase) {
  if (!GENTLE_ANIMATIONS) {
    return 1.0;
  }
  float x = (now % periodMs) / (float)periodMs + phase;
  return 0.5 - 0.5 * cos(2 * PI * x);
}

// Colour along the sunset: t = 0 is gold, t = 1 is deep blue
void sunsetColour(float t, float* r, float* g, float* b) {
  t = constrain(t, 0, 1);
  float pos = t * (SUNSET_STOPS - 1);
  int i = min((int)pos, SUNSET_STOPS - 2);
  float f = pos - i;
  *r = SUNSET_COLOURS[i][0] + (SUNSET_COLOURS[i + 1][0] - SUNSET_COLOURS[i][0]) * f;
  *g = SUNSET_COLOURS[i][1] + (SUNSET_COLOURS[i + 1][1] - SUNSET_COLOURS[i][1]) * f;
  *b = SUNSET_COLOURS[i][2] + (SUNSET_COLOURS[i + 1][2] - SUNSET_COLOURS[i][2]) * f;
}

// Each star twinkles in its own rhythm
void drawStars(unsigned long now, int count) {
  for (int s = 0; s < count && s < 4; s++) {
    float k = 0.6 + 0.4 * wave(now, 1500, s * 0.25);
    setPixel(STAR_PIXELS[s], 255 * k, 255 * k, 255 * k);
  }
}

void drawFrame(unsigned long now) {
  memset(frame, 0, sizeof(frame));

  // How many pixels show the remaining time (all 72 at the start of a phase)
  float fraction = (phaseTotalMs > 0) ? (float)phaseRemainingMs / phaseTotalMs : 0;
  int lit = (int)ceil(fraction * NUM_LEDS);

  switch (state) {
    case SETUP_TIME: {
      // Preview: lit pixels show the chosen time out of the 60-minute maximum
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
          k = 0.4 + 0.6 * wave(now, 1000, 0);        // the whole sun pulses at the end
        } else if (i == lit - 1) {
          k = 0.3 + 0.7 * wave(now, 2000, 0);        // the next pixel to go out breathes
        }
        setPixel(i, 255 * k, 150 * k, 40 * k);
      }
      break;
    }

    case PAUSED_MANUAL:
    case PAUSED_AWAY: {
      float k = 0.1 + 0.2 * wave(now, 3000, 0);      // slow, dim breathing
      for (int i = 0; i < lit; i++) {
        setPixel(i, 255 * k, 150 * k, 40 * k);
      }
      break;
    }

    case PAUSED_PHONE: {
      float k = 0.2 + 0.8 * wave(now, 800, 0);       // urgent orange pulse
      for (int i = 0; i < lit; i++) {
        setPixel(i, 255 * k, 60 * k, 0);
      }
      break;
    }

    case SUNSET: {
      // A wave of colour sweeps along the pixels, each a little behind the one before
      float t = (float)(now - stateStartMs) / SUNSET_MS;
      for (int i = 0; i < NUM_LEDS; i++) {
        float r, g, b;
        sunsetColour(t * 1.6 - ((float)i / NUM_LEDS) * 0.6, &r, &g, &b);
        setPixel(i, r, g, b);
      }
      if (t > 0.85) {
        drawStars(now, cycleStars + 1);              // the new star appears at the end
      }
      break;
    }

    case SHORT_BREAK:
    case LONG_BREAK: {
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i < lit) setPixel(i, 20, 40, 160);       // break time left
        else         setPixel(i, 3, 5, 20);          // night sky
      }
      drawStars(now, cycleStars);
      break;
    }

    case DAWN: {
      // The sunset played backwards
      float t = (float)(now - stateStartMs) / DAWN_MS;
      for (int i = 0; i < NUM_LEDS; i++) {
        float r, g, b;
        sunsetColour(1.0 - (t * 1.6 - ((float)i / NUM_LEDS) * 0.6), &r, &g, &b);
        setPixel(i, r, g, b);
      }
      break;
    }
  }
}

// Draws a frame at most 5 times per second, and only publishes it when it changed
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
