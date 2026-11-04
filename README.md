# Pomodoro Sunset

**A focus timer that turns the Vespera light installation into a setting sun.**

Every focus session is a "day" on Vespera: its 72 pixels go out one by one as time runs down,
a sunset sweeps across the light when the session ends, and an evening star appears for each
completed pomodoro. The timer only runs while your phone lies face down on the controller, and
it pauses when you leave your desk.

<img src="docs/images/enclosure/2026-11-04_enclosure-finished.jpg" width="600" alt="Pomodoro Sunset controller">

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

<img src="docs/images/diagrams/2026-11-04_system-diagram.png" width="700" alt="System diagram: sensors to MKR WiFi 1010, which drives the LCD and publishes over WiFi to the MQTT broker, which sends frames to Vespera and Lumi">

- **Broker:** `mqtt.cetools.org`, port 1884
- **Topic:** `student/CASA0014/luminaire/29` (the only topic this device publishes to)
- **Payload:** 216 bytes = R, G, B for each of the 72 pixels, sent as one message per frame
- **Rate:** at most 5 frames per second, and only when the frame changes
- **Timer:** runs locally on the board, so the countdown continues if the network drops

### Light states

One cycle as seen in the Lumi web app (luminaire 29). The states run
setup → focus → sunset → break → dawn → setup.

| 1. Setup | 2. Focus | 3. Sunset |
|---|---|---|
| <img src="docs/images/states/2026-11-03_state-1-setup.png" width="260" alt="Setup: six pixels lit"> | <img src="docs/images/states/2026-11-03_state-2-focus.png" width="260" alt="Focus: pixels going out one by one"> | <img src="docs/images/states/2026-11-03_state-3-sunset.png" width="260" alt="Sunset gradient sweeping across the pixels"> |
| 5 minutes chosen: 5 / 60 × 72 = 6 pixels preview the time | Pixels go out one by one; the next to go out (56) breathes | A gradient from gold to deep blue sweeps across the light |

| 4. Break | 5. Dawn |
|---|---|
| <img src="docs/images/states/2026-11-03_state-4-break.png" width="260" alt="Break: blue countdown with two white stars"> | <img src="docs/images/states/2026-11-03_state-5-dawn.png" width="260" alt="Dawn: the sunset gradient in reverse"> |
| Blue countdown; the two white pixels (0 and 18) are stars for two completed pomodoros | The sunset in reverse, back to setup |

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

**Wiring plan (26 Oct)**, drawn before building:

<img src="docs/images/wiring/2026-10-26_wiring-plan-hand-drawn.jpg" width="480" alt="Hand-drawn wiring plan">

**Current wiring (v2, 2 Nov).** The earlier build is compared in [Iterations](#9-iterations).

<img src="docs/images/wiring/2026-11-02_wiring-v2-overview.jpg" width="480" alt="Breadboard wiring v2">

### Modules

| Rotary encoder | LDR phone dock | MKR WiFi 1010 |
|---|---|---|
| <img src="docs/images/modules/2026-10-27_module-encoder.jpg" width="220" alt="Rotary encoder on the breadboard"> | <img src="docs/images/modules/2026-10-27_module-ldr.jpg" width="220" alt="LDR with its 10k resistor"> | <img src="docs/images/modules/2026-10-27_module-mkr1010.jpg" width="220" alt="Arduino MKR WiFi 1010 on the breadboard"> |
| Sets the focus time; press to start, hold to reset | Detects the phone lying face down | Reads the sensors and sends frames over WiFi / MQTT |

| Encoder and potentiometer, wired for the enclosure | LDR soldered to extension wires |
|---|---|
| <img src="docs/images/wiring/2026-11-02_encoder-pot-wiring.jpg" width="220" alt="Encoder and potentiometer with jumper wires"> | <img src="docs/images/modules/2026-10-30_ldr-soldered.jpg" width="220" alt="LDR soldered to extension wires"> |

| LCD1602 RGB and HC-SR04 |
|---|
| <img src="docs/images/modules/YYYY-MM-DD_module-lcd-hcsr04.jpg" width="300" alt="LCD1602 RGB module and HC-SR04 ultrasonic sensor"> |
| The LCD shows the exact time and its backlight follows Vespera; the HC-SR04 faces the user and its echo line goes through a 1k + 2k divider |

## 5. Enclosure

A medium-fidelity cardboard enclosure: a sloped desk console with the screen on the slope,
the phone dock and knobs on the top, and the ultrasonic sensor facing the user.

| Sketch (30 Oct) | Cut panel | Empty shell |
|---|---|---|
| <img src="docs/images/enclosure/2026-10-30_enclosure-sketch.jpg" width="220" alt="Enclosure sketch"> | <img src="docs/images/enclosure/2026-10-30_enclosure-panel-cut.jpg" width="220" alt="Cardboard panel with cut-outs for the screen and knobs"> | <img src="docs/images/enclosure/2026-10-30_enclosure-empty.jpg" width="220" alt="Empty cardboard enclosure"> |
| Inspired by a desk clock: the screen is tilted so the time is easy to read, with a flat platform on top for the phone | Openings sized by measuring each part, then cut and trimmed until it fitted | The cardboard was partly cut through so it could bend at the folds; tabs were left for joining, and it was taped into shape |

| Planning the layout (2 Nov) | Wired inside |
|---|---|
| <img src="docs/images/enclosure/2026-11-02_enclosure-components-layout.jpg" width="300" alt="Components laid out on the cardboard"> | <img src="docs/images/wiring/2026-11-02_enclosure-inside-wiring.jpg" width="300" alt="Electronics wired inside the enclosure"> |
| HC-SR04 at the bottom facing the user, LCD on the slope, and the LDR, encoder and potentiometer on top | Everything fitted inside the enclosure |

**Hardest part: the encoder.** Its knob is also a push button, so it needs firm support
from inside, otherwise the cardboard bends when it is pressed. It was also difficult to fix
it to the cardboard. I solved this with an internal support piece and a hex nut, which hold
the encoder firmly in the top of the enclosure.

**Finished controller**

<img src="docs/images/enclosure/2026-11-04_enclosure-finished.jpg" width="480" alt="Finished cardboard controller with all parts fitted">

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

### Firmware v1 → v2

- **v1** (`firmware/pomodoro_sunset/`): the first full prototype, split across five
  Arduino tabs. As well as the Vespera frames, it published a JSON status topic with a
  retained "offline" last-will message.
- **Feedback:** on the shared broker, students may only publish to their own luminaire topic.
- **v2** (`firmware/pomodoro_sunset_v2/`): the status topic and last-will message were
  removed, and the tabs were merged into one file.

### LDR thresholds

The first threshold (docked below 40% of room light) never triggered: the LDR read about
700 uncovered and only about 400 covered. It was changed to 75% (docked) and 85% (removed).

### Wiring v1 → v2

| v1 (27 Oct) | v2 (2 Nov) |
|---|---|
| <img src="docs/images/wiring/2026-10-27_wiring-v1-overview.jpg" width="300" alt="First breadboard build"> | <img src="docs/images/wiring/2026-11-02_wiring-v2-overview.jpg" width="300" alt="Rebuilt wiring"> |
| Wires added one at a time, crossing over the MKR1010 | Rebuilt by zones: each group of wires leaves from its own side of the board |

### LDR moved into the enclosure

TODO: why the LDR was soldered to longer wires, and what changed when it was re-tested
inside the case.

### Presence detection

TODO: seated and away distances, and the new `PRESENCE_MAX_CM`. The away delay felt too
long at 10 s, so it was shortened to 5 s in demo mode.

### Countdown colour

TODO: the original gold (255, 150, 40) looked pale on Vespera's acrylic, so it was changed
to (255, 100, 0). Add before / after photos.

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
- I used Claude (Anthropic) to help draft parts of the firmware and this README.
  I tested all code on the hardware, chose the design, took all measurements and photos,
  and edited the documentation in my own words.

Released under the MIT License.