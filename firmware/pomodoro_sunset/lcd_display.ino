// lcd_display.ino - a small driver for the Waveshare LCD1602 RGB Module (I2C)
//
// The module has two chips on the I2C bus (SDA = pin 11, SCL = pin 12):
//   AiP31068 LCD controller at address 0x3E (standard HD44780 commands)
//   PCA9633 RGB backlight driver, normally at 0x60
// Talking to them directly means no extra library has to be installed.

const uint8_t LCD_ADDR = 0x3E;
const uint8_t RGB_CANDIDATES[] = {0x60, 0x62};
uint8_t rgbAddr = 0;                   // 0 = backlight chip not found

char lcdLines[2][17] = {"", ""};       // what is on the screen now
char messageLines[2][17];              // a temporary message
unsigned long messageUntilMs = 0;
unsigned long lastLcdMs = 0;
int lastBacklight[3] = {-1, -1, -1};


bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void lcdCommand(uint8_t cmd) {
  Wire.beginTransmission(LCD_ADDR);
  Wire.write(0x80);                    // control byte: a command follows
  Wire.write(cmd);
  Wire.endTransmission();
}

// Writes one row, padded with spaces so old characters are cleared
void lcdWriteRow(uint8_t row, const char* text) {
  lcdCommand(row == 0 ? 0x80 : 0xC0);  // move the cursor to the start of the row
  Wire.beginTransmission(LCD_ADDR);
  Wire.write(0x40);                    // control byte: characters follow
  int i = 0;
  for (; i < 16 && text[i] != '\0'; i++) {
    Wire.write(text[i]);
  }
  for (; i < 16; i++) {
    Wire.write(' ');
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

  // List every I2C device - the first thing to check if the screen stays blank
  Serial.print("I2C devices found:");
  for (uint8_t a = 1; a < 127; a++) {
    if (i2cDevicePresent(a)) {
      Serial.print(" 0x");
      Serial.print(a, HEX);
    }
  }
  Serial.println();
  if (!i2cDevicePresent(LCD_ADDR)) {
    Serial.println("LCD controller not found at 0x3E - check VCC, GND, SDA (11) and SCL (12)");
  }

  // Start-up sequence: 2 lines, 5x8 font, display on, clear, text runs left to right
  lcdCommand(0x28); delay(5);
  lcdCommand(0x28); delay(1);
  lcdCommand(0x28);
  lcdCommand(0x0C);                    // display on, no cursor, no blinking
  lcdCommand(0x01); delay(2);          // clear the screen
  lcdCommand(0x06);                    // entry mode

  for (uint8_t i = 0; i < sizeof(RGB_CANDIDATES); i++) {
    if (i2cDevicePresent(RGB_CANDIDATES[i])) {
      rgbAddr = RGB_CANDIDATES[i];
      break;
    }
  }
  if (rgbAddr == 0) {
    Serial.println("RGB backlight chip not found - the text works but colours will not");
  } else {
    rgbRegister(0x00, 0x00);           // MODE1: normal operation
    rgbRegister(0x08, 0xFF);           // LEDOUT: each colour set by its own PWM register
    rgbRegister(0x01, 0x20);           // MODE2
  }
}

void lcdBacklight(int r, int g, int b) {
  if (r == lastBacklight[0] && g == lastBacklight[1] && b == lastBacklight[2]) {
    return;                            // nothing changed - keep the I2C bus quiet
  }
  rgbRegister(0x04, r);                // red   (swap these three if colours look wrong)
  rgbRegister(0x03, g);                // green
  rgbRegister(0x02, b);                // blue
  lastBacklight[0] = r;
  lastBacklight[1] = g;
  lastBacklight[2] = b;
}

// Writes both rows immediately (used during start-up)
void lcdShow(const char* line1, const char* line2) {
  lcdWriteRow(0, line1);
  lcdWriteRow(1, line2);
  strncpy(lcdLines[0], line1, 16);
  lcdLines[0][16] = '\0';
  strncpy(lcdLines[1], line2, 16);
  lcdLines[1][16] = '\0';
}

// A short message that replaces the normal screen for a while
void showLcdMessage(const char* line1, const char* line2, unsigned long durationMs) {
  strncpy(messageLines[0], line1, 16);
  messageLines[0][16] = '\0';
  strncpy(messageLines[1], line2, 16);
  messageLines[1][16] = '\0';
  messageUntilMs = millis() + durationMs;
  lastLcdMs = 0;                       // refresh on the next loop
}

// The text for each state - every line is 16 characters or fewer
void buildLcdText(char* line1, char* line2) {
  unsigned long secs = (phaseRemainingMs + 999) / 1000;     // round up
  int mm = secs / 60;
  int ss = secs % 60;

  char stars[5];                        // e.g. "**--" = two pomodoros done in this cycle
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

// The backlight colour mirrors what Vespera is showing
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
  float k = max(outputBrightness(), 0.25f);                 // keep the screen readable
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

  // Only rewrite rows that changed - avoids flicker
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
