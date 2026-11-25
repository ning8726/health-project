# Health project: one OLED, Arduino + Yun + Windows/macOS checker

This is the updated version of your UNO R4 Minima health / exercise project.
It uses **one OLED that cycles through heart rate -> repetitions -> movement**,
not three separately addressed displays. Disconnect the other two OLEDs.
The ultrasonic sensor is removed. The finger sensor stays attached, and the
accelerometer stays on the moving forearm.

The code and wiring target the hardware we previously selected: UNO R4 Minima,
Dragino Yun shield, sensor shield, a MAX3010x heart-rate breakout, ADXL345 or
BMA150,
Grove/digital touch sensor, and a 128x64 SSD1306/SSD1315-compatible I2C OLED.
Your photos do not establish every sensor pin label or voltage rating. Confirm
the actual labels before wiring. This is not firmware for an ESP32, ESP8266,
UNO R3, MPU-6050, or SH1106 display. See `WINDOWS_AND_GIT.md` for Windows and
Git commands.

## Files to use

| File | Purpose |
|---|---|
| `R4_Health/R4_Health.ino` | Complete, self-contained Arduino sketch |
| `wiring.png` / `wiring.pdf` / `wiring.svg` | Exact labelled wiring diagram |
| `yun/collector.py` | Reads UNO USB serial on the Yun; publishes current JSON |
| `yun/index.html` | Phone / Windows / Mac live dashboard; no internet assets required |
| `install_from_mac.sh` | Copies the Yun files over SSH and starts the collector |
| `mac/check_health.py` | Cross-platform checker: direct USB, or the Yun over Wi-Fi |
| `mac/Run_Mac_Checker.command` | Menu to start the Mac checker |
| `windows/Run_Health_Checker.bat` | Double-click Windows USB checker |
| `windows/install_to_yun.ps1` | Windows PowerShell Yun installer |
| `WINDOWS_AND_GIT.md` | Windows setup and Git/GitHub workflow |
| `VALIDATION.md` | What was checked, and the hardware checks still needed |

## 1. Wire the components

**Power off first.** Keep the existing aligned board stack: UNO at the bottom,
Yun in the middle, sensor shield on top. Maintain clearance around metal
connectors. The diagram shows functional connections, not the exact physical
locations of the pins on your shield. Use the printed pin names.

| Component | Component pin | UNO / top-shield connection |
|---|---|---|
| One OLED | VCC | **3V3**, if the breakout accepts 3.3 V input |
| One OLED | GND | GND |
| One OLED | SDA | A4 |
| One OLED | SCL | A5 |
| Heart-rate breakout | VIN / VCC | **3V3**, if the breakout accepts 3.3 V input |
| Heart-rate breakout | GND | GND |
| Heart-rate breakout | SDA | A0 |
| Heart-rate breakout | SCL | A1 |
| Heart-rate breakout | INT | Unconnected |
| ADXL345 breakout | VCC / VIN | **3V3**, if the breakout accepts 3.3 V input |
| ADXL345 breakout | GND | GND |
| ADXL345 breakout | SDA | A2 |
| ADXL345 breakout | SCL | A3 |
| ADXL345 breakout | CS | 3V3 (selects I2C) |
| ADXL345 breakout | SDO / ALT ADDRESS | GND (selects address 0x53) |
| ADXL345 breakout | INT1 / INT2 | Unconnected |
| **BMA150** breakout | VCC | **3V3 only** |
| **BMA150** breakout | GND | GND |
| **BMA150** breakout | SDA | A2 |
| **BMA150** breakout | SCL | A3 |
| **BMA150** breakout | CSB | 3V3 (selects I2C) |
| **BMA150** breakout | INT1 / INT2 | Unconnected |
| Touch sensor | VCC | 5V |
| Touch sensor | GND | GND |
| Touch sensor | SIG / OUT | D5 |
| Touch sensor | Unused second signal, if present | Unconnected |
| Yun | USB-A HOST | UNO USB-C, using a USB **data** cable |

All grounds are common. Repeated 3V3 / GND labels in the drawing mean the
same power rails; use the breadboard to distribute them. A shield socket's
VCC may be 5 V even when a nearby label says 3V3. Take sensor power from the
UNO's labelled 3V3 pin rather than an unchecked shield VCC socket.

The three I2C pairs are separate: **A0/A1, A2/A3, and A4/A5 must not be joined**.
Use short, secure leads and the breakout modules' existing low-voltage pull-ups.
The common 7-pin BMA150 module has no exposed SDO pin; that is expected. Its
fixed I2C address is `0x38`. Connect `CSB` to 3V3 and leave `INT1`/`INT2`
unconnected.
The sketch never drives these signal pins HIGH and never enables 5 V pull-ups;
it reads their HIGH level with the R4 ADC. This retains the previous prototype
approach without a separate level-shifter board. If a module lacks pull-ups,
does not accept a 3.3 V input, or has different pin labels, this connection
has to be adapted to that module before use.

The power input of a breakout is not the bare chip's VDD pin. In particular,
the MAX30102 IC itself uses a 1.8 V core supply; a compatible breakout supplies
this through its own regulator. Do not connect 3.3 V directly to a bare 1.8 V VDD.

### Power and USB modes

- **Upload or direct computer test:** connect Windows or macOS to the UNO USB-C port.
- **Yun Wi-Fi operation:** connect the Yun USB-A **host** port to UNO USB-C.
  The stack headers alone do not supply this USB data connection.
- The UNO has one USB-C port. Move its cable between the computer and Yun; do not try
  to connect both USB hosts to that port at once.
- For powered Yun operation, retain the correctly rated existing stack supply.
  The earlier package specified a regulated 7-12 V centre-positive supply to
  the UNO barrel jack. Confirm your actual Yun revision's input rating and
  factory power jumpers before connecting an adapter. The Yun processor and
  USB host require adequate power; a working UNO USB connection by itself
  does not establish that the Yun is powered correctly.

## 2. Upload the Arduino code

1. In Arduino IDE Boards Manager, install **Arduino UNO R4 Boards**.
2. In Library Manager, install **SparkFun MAX3010x Pulse and Proximity Sensor Library**.
3. Open `R4_Health/R4_Health.ino`. This is the complete sketch; do not paste the
   old ultrasonic code into it. No additional local Arduino tabs are required.
4. Select **Arduino UNO R4 Minima** and the UNO USB port. Verify and upload.
5. Close Arduino IDE Serial Monitor before starting the local checker.

The included drivers handle the ADXL345, BMA150 and the OLED. SparkFun's `heartRate.h`
provides the optical beat-detection algorithm. The heart driver checks the
part ID and supports the MAX30100 or MAX30102/05 family at address 0x57;
the ADXL345 must report device ID 0xE5 at 0x53 or 0x1D. The BMA150 must report
chip ID 0x02 at fixed address 0x38.

### What the OLED displays

| Page | Information |
|---|---|
| 1 / 3 | Estimated BPM, acquisition status, raw IR, movement warning |
| 2 / 3 | Reps / goal, session state, setup prompt, calibrated span, elapsed time |
| 3 / 3 | Movement consistency, stationary tilt, last-rep / reference duration |

Pages change automatically every **4 seconds**. During pose calibration,
the display stays on page 2 so the setup prompt remains visible.
Missing values show `--`. The other two physical OLEDs stay disconnected.

## 3. Check it directly from Windows or macOS first

The checker requires **Python 3**. macOS needs no additional packages. Windows
needs `pyserial`; the supplied launcher installs it on first use. The checker
opens a local browser page; keep its terminal or command window running.

On Windows, double-click:

```text
windows\Run_Health_Checker.bat
```

Or run from PowerShell:

```powershell
py -3 mac\check_health.py --usb
py -3 mac\check_health.py --list-ports
py -3 mac\check_health.py --usb COM3
```

On macOS:

```sh
python3 mac/check_health.py --usb
python3 mac/check_health.py --list-ports
python3 mac/check_health.py --usb /dev/cu.usbmodemYOUR_PORT
sh mac/Run_Mac_Checker.command
```

It opens `http://127.0.0.1:8770/`. Use the actual port printed by
`--list-ports`.

The checker displays BPM, reps, motion, sensor detection, the Arduino's
reported OLED page, packet counts and the raw diagnostics. It is read-only;
use the physical touch pad to control the session. Control-C stops it.

If port 8770 is in use, add `--port 8771` and use the address it prints.

## 4. Install the Yun Wi-Fi software

The Dragino Yun already runs Linux. **Do not upload a second Arduino sketch
to the Wi-Fi board.** Install the Python collector and web page with SSH.

1. Connect the Yun to power and use its existing Wi-Fi configuration.
2. Connect your computer to the Yun's network, or the same network as the Yun.
3. Move the UNO USB-C cable to the Yun USB-A host port.
4. In the extracted `Health_Project` folder on macOS, run:

```sh
sh install_from_mac.sh YOUR_YUN_IP
```

On Windows PowerShell, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\install_to_yun.ps1 -YunHost YOUR_YUN_IP
```

Replace `YOUR_YUN_IP` with the Yun's actual address, for example
`192.168.240.1` if that is still its configured address. Enter the Yun's
current root password when SSH requests it. It is not stored in these files.

This installs the new protocol-2 collector and page, replacing the earlier
workout reader at the same location. It preserves Wi-Fi settings, firmware,
firewall configuration, UART console, and SSH host verification.

The Yun needs an available Python 2.7 or 3 interpreter and a USB CDC ACM
driver exposing the UNO as `/dev/ttyACM0` or a similar device. The installer
checks the existing driver; it does not flash the Yun or force-install
kernel packages from another firmware release.

Open the phone dashboard on the same local network:

```text
http://YOUR_YUN_IP/workout/
```

To view the same real readings through the macOS checker:

```sh
python3 mac/check_health.py --wifi http://YOUR_YUN_IP/workout/latest.json
```

The collector sends no readings to a cloud service. Keep this on your local
network; no public port forwarding is required. The checker detects
frozen JSON files by requiring the Arduino packet sequence to advance.

Yun diagnostics:

```sh
cat /tmp/workout/collector.log
cat /tmp/workout/latest.json
/etc/init.d/workout-monitor stop
/etc/init.d/workout-monitor start
```

These commands run on the Yun after connecting by SSH.

## 5. Save poses and count repetitions

This version counts **changes in forearm orientation**, not ultrasonic
distance. Use a slow movement, such as an arm raise, that changes the
accelerometer's orientation relative to gravity. It cannot reliably count a pure
translation with unchanged orientation, or rotation around gravity.
The reported tilt is not an elbow-joint angle.

1. Secure the ADXL345 or BMA150 so it does not slide. Attach the finger holder with light,
   comfortable contact. Leave cable slack around the wrist.
2. Tap and release the touch pad to enter setup.
3. Hold the start pose still for about one second. Tap to save it.
4. Move to the target pose and hold still. Tap to save it. The two gravity
   directions must differ by at least **25 degrees**; otherwise the program
   asks for a larger change. This is a configurable detection threshold,
   not a prescribed range of motion.
5. Tap to start the set. Go to the saved start pose.
6. Reach the target and return to the start, pausing about one second at each
   endpoint. Only a full start -> target -> return cycle counts.
7. Tap to pause/resume. Hold the touch pad for **2 seconds** to clear calibration.
8. The first fully observed valid repetition becomes the movement reference.
   Later repetitions are compared with that reference for duration and an
   acceleration-change measure. A counted repetition is not a verified form score.

The default goal is 10 reps. Change `REP_GOAL` near the application section
in the sketch if needed. Calibration is not saved through a power cycle.

### Detection details

- Endpoint hold: 600 ms **after** the arm is judged stable; allow about one second.
- Minimum completed cycle: 1000 ms. A cycle over 30 seconds is cancelled.
- Missing fresh accelerometer data prevents a new count; a gap cancels the partial cycle.
- A new set clears the movement reference. Pause/resume also cancels a partial cycle.
- The OLED transfers one 16-byte chunk per loop so sensor sampling can continue.
- Sensor detection retries every five seconds when a component is absent.
- Telemetry is newline-delimited protocol-2 JSON, queued about every 750 ms.

The pulse sensor remains attached and sampling throughout. Obtaining a stable
optical pulse estimate takes several seconds of usable contact, and movement
can create artifacts. The screen and dashboard identify estimates and show a
movement warning; they do not certify accuracy. There is no SpO2 calculation,
diagnosis, fatigue assessment, or clinically validated movement assessment.

## Troubleshooting

| Symptom | Check |
|---|---|
| OLED blank | Use only one screen; VCC rating, GND, SDA A4, SCL A5; SSD1306-compatible 128x64 controller |
| Heart sensor missing | Breakout VIN rating, GND, SDA A0, SCL A1; module part ID and onboard pull-ups |
| No pulse number yet | Light finger contact; hold still; inspect raw IR and acquisition status |
| Accelerometer missing | ADXL345: CS high, SDO grounded. BMA150: CSB high, no SDO pin. Both: 3V3, GND, SDA A2, SCL A3 |
| Tilt blank | Save the start pose and hold still; check fresh data |
| Reps stay at zero | Save distinct poses, wait at each endpoint, use a movement that changes sensor tilt |
| Windows/macOS cannot open USB | Close Serial Monitor; use a data cable; use `--list-ports` and specify COM/USB port |
| Yun has no ttyACM port | Host USB data cable, power, and driver matching that Yun firmware |
| Wi-Fi dashboard shows old layout | Install the new Yun files as well as uploading the new sketch |
| Live values become -- | No usable sensor reading or no advancing packet; inspect collector status |

Raw diagnostics include idle bus ADC readings. With a 12-bit ADC and a nominal
5 V reference, a bus idling at 3.3 V is around 2700 counts and a 1.8 V bus
around 1470. Actual values vary. Zero suggests a low / unpowered bus; values
near 4095 suggest a 5 V-level bus. The custom bus rejects an excessively high
or low idle level rather than treating a bad connection as good data.

## Optional interface demonstration

```sh
python3 mac/check_health.py --demo
```

This is labelled **SIMULATED DATA**. It checks the local interface without
hardware and must not be presented as measurements from your project.

## Source references

- Arduino UNO R4 Minima: https://docs.arduino.cc/tutorials/uno-r4-minima/cheat-sheet/
- Arduino board setup: https://docs.arduino.cc/tutorials/uno-r4-minima/minima-getting-started/
- Dragino Yun manual: https://www.dragino.com/downloads/downloads/YunShield/YUN_SHIELD_USER_MANUAL_v1.0.pdf
- BMA150 datasheet: https://media.digikey.com/pdf/Data%20Sheets/Bosch/BMA150.pdf
- MAX30102 datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf
- ADXL345 datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL345.pdf
- Accelerometer inclination: https://www.analog.com/en/resources/app-notes/an-1057.html
- SparkFun beat detector dependency: https://github.com/sparkfun/SparkFun_MAX3010x_Sensor_Library

The custom sensor / OLED drivers were adapted from your earlier project
package. Install the SparkFun library through Arduino Library Manager; its
third-party source is not bundled here. See `VALIDATION.md` before demonstration.
