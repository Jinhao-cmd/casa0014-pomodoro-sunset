# Pomodoro Sunset

**A focus timer that turns the Vespera light installation into a setting sun.**

Every focus session is a "day" on Vespera: its 72 pixels go out one by one as time runs down,
a sunset sweeps across the light when the session ends, and an evening star appears for each
completed pomodoro. The timer only runs while your phone lies face down on the controller, and
it pauses when you leave your desk.

<!-- TODO: add a hero photo of the controller next to Vespera, then delete these comment marks
![Pomodoro Sunset controller with Vespera](docs/images/hero.jpg)
-->

- **Course:** CASA0014 Prototyping the Internet of Things, UCL CASA, 2026
- **Vespera luminaire:** 29 (MQTT topic `student/CASA0014/luminaire/29`)
- **Demo video:** TODO add link

## Contents

1. [Concept and inspiration](#1-concept-and-inspiration)
2. [How it works](#2-how-it-works)
3. [System design](#3-system-design)
4. [Hardware](#4-hardware)
5. [Enclosure](#5-enclosure)
6. [Software](#6-software)
7. [Build your own](#7-build-your-own)
8. [Testing and calibration](#8-testing-and-calibration)
9. [Iterations](#9-iterations)
10. [Naming conventions](#10-naming-conventions)
11. [Operational notes](#11-operational-notes)
12. [Limitations and future work](#12-limitations-and-future-work)
13. [Acknowledgements](#13-acknowledgements)

---

## 1. Concept and inspiration

*Vespera* is Latin for "evening" and the name of the evening star. The installation represents
the passage of time from day to night, so I designed a controller that is about time:
a Pomodoro timer, where each 25-minute focus session becomes a day that sets into evening.

- **The Pomodoro Technique** takes its name from a tomato-shaped kitchen timer that you twist
  to start. The rotary knob keeps that gesture.
- **A phone dock** turns starting a session into a commitment: put your phone face down to begin.
- **A shared signal:** Vespera hangs in a shared lab, so the light also tells others that
  someone is focusing.

TODO: add concept sketches and explain the ideas you considered before this one.

<!-- ![Concept sketch](docs/images/YYYY-MM-DD_concept-sketch.jpg) -->

## 2. How it works

| Action | What happens |
|---|---|
| Turn the knob | Choose the focus length (5-60 min); Vespera previews it |
| Press the knob | Start, pause or resume; skips a break |
| Hold the knob for 1.5 s | Reset |
| Lift your phone from the dock | Timer pauses, Vespera pulses orange, an interruption is counted |
| Leave your desk | Timer pauses until you come back |
| Turn the small knob | Brightness of Vespera and the screen |

| Stage | Vespera | LCD |
|---|---|---|
| Focus | Warm gold; one pixel goes out roughly every 21 s | `Focus 18:42` |
| Last minute | The remaining "sun" pulses | Countdown |
| End of focus | Sunset sweep, then an evening star appears | `Focus complete!` |
| Break | Blue countdown under the stars | `Break 04:12` |
| Four pomodoros | Long break under a full night sky | `Long brk` |

## 3. System design

TODO: add a system diagram.

```
Encoder, potentiometer, LDR, HC-SR04 --> MKR WiFi 1010 --> WiFi --> MQTT broker --> Vespera / Lumi
                                              |
                                              +--> LCD1602 RGB (local display)
```

- **Broker:** `mqtt.cetools.org`, port 1884
- **Topic:** `student/CASA0014/luminaire/29` (the only topic this device publishes to)
- **Payload:** 216 bytes = R, G, B for each of the 72 pixels, sent as one message per frame
- **Rate:** at most 5 frames per second, and only when the frame changes
- **Timer:** runs locally on the board, so the countdown continues if the network drops

TODO: add the state diagram (setup, focus, paused, sunset, break, dawn).

## 4. Hardware

### Bill of materials

| Part | Qty | Notes |
|---|---|---|
| Arduino MKR WiFi 1010 | 1 | |
| Rotary encoder with push button | 1 | Main control |
| Potentiometer | 1 | Brightness |
| LDR (photoresistor) + 10 kΩ resistor | 1 + 1 | Phone dock |
| HC-SR04 ultrasonic sensor (5 V version) | 1 | Presence |
| 1 kΩ resistors | 3 | Voltage divider for the HC-SR04 echo pin |
| Waveshare LCD1602 RGB module | 1 | I2C |
| Breadboard and jumper wires | | |

### Wiring

| Part | Pin on the MKR1010 |
|---|---|
| Encoder CLK / DT / SW | 4 / 5 / 3 (encoder + to VCC) |
| Potentiometer middle leg | A1 (outer legs to VCC and GND) |
| LDR divider | VCC -> LDR -> A2 -> 10 kΩ -> GND |
| HC-SR04 VCC / Trig | 5V / 7 |
| HC-SR04 Echo | 1 kΩ + 2 kΩ divider -> 8 |
| LCD1602 RGB | VCC, GND, SDA (11), SCL (12) |

> **Warning:** the HC-SR04 echo pin outputs 5 V, but the MKR1010 pins accept only 3.3 V.
> Always use the voltage divider.

<!-- ![Wiring from above](docs/images/YYYY-MM-DD_wiring-top.jpg) -->

## 5. Enclosure

A medium-fidelity cardboard enclosure: a sloped desk console with the screen on the slope,
the phone dock and knobs on the top, and the ultrasonic sensor facing the user.

TODO: add photos of the paper mock-up, the cardboard net and the finished enclosure.

## 6. Software

```
firmware/pomodoro_sunset_v2/
  pomodoro_sunset_v2.ino        the complete sketch
  arduino_secrets.example.h     template for your WiFi / MQTT passwords
tests/                          small sketches used to test each sensor
docs/images/                    sketches, photos and screenshots
```

Libraries: WiFiNINA, Arduino_SpiNINA, PubSubClient (Nick O'Leary).
The LCD is driven directly over I2C, so no LCD library is needed.

Main settings at the top of the sketch:

| Setting | Purpose |
|---|---|
| `LIGHT_ID` | Vespera luminaire number |
| `DEMO_MODE` | One minute lasts 4 seconds, for demos |
| `REQUIRE_PHONE_DOCK`, `USE_PRESENCE` | Switch sensors on or off while testing |
| `PHONE_ON_RATIO`, `PHONE_OFF_RATIO` | LDR thresholds |
| `PIXEL_ORDER`, `STAR_PIXELS` | Map the animation to Vespera's physical layout |

## 7. Build your own

1. Wire the parts as in [Hardware](#4-hardware).
2. Install the libraries listed in [Software](#6-software).
3. Copy `arduino_secrets.example.h` to `arduino_secrets.h` and add your WiFi and MQTT details.
4. Set `LIGHT_ID` to your own luminaire number.
5. Upload, keep the phone dock empty while the screen shows `Keep dock empty`.
6. Select your luminaire number in the Lumi web app to see the output.

## 8. Testing and calibration

TODO: add Serial Plotter screenshots.

- **LDR phone dock:** about 700 uncovered and 400 covered in the lab. The first threshold
  (40% of room light) never triggered, so it was changed to 75% / 85% (with hysteresis).
- **HC-SR04:** TODO record distances when seated and away.

## 9. Iterations

TODO: describe each version and why it changed, with photos.

## 10. Naming conventions

- **MQTT:** `student/CASA0014/luminaire/<id>`, as set by the course
- **Images:** `YYYY-MM-DD_short-description.jpg`
- **Commits:** start with a verb (`Add`, `Fix`, `Update`, `Tune`)

## 11. Operational notes

- Power from USB (computer or power bank): the HC-SR04 needs the 5 V USB supply.
- The light level is calibrated at start-up: keep the phone dock empty until the screen changes.
- The physical Vespera shows one luminaire at a time; check `"user"` in
  `student/CASA0014/luminaire/status`.
- TODO: troubleshooting table.

## 12. Limitations and future work

TODO

## 13. Acknowledgements

- Vespera, the Lumi web app and the workshop sketches by Duncan Wilson, UCL CASA
  Connected Environments Lab
- TODO: acknowledge any other help or tools used, following the module's guidance

Released under the MIT License.
