# Arduino Health and Exercise Monitor

An Arduino UNO R4 Minima prototype for estimating heart rate, counting
exercise repetitions, and comparing movement consistency. Live data is shown
on a 128x64 OLED and in a local browser dashboard on Windows, macOS, or a
Dragino Yun.

> This is an engineering and course prototype. Heart-rate values are
> estimates and must not be used for medical diagnosis. The project does not
> calculate SpO2, fatigue, or clinically validated exercise form.

![Wiring diagram](wiring.png)

## Features

- Estimate heart rate and show pulse-acquisition status.
- Detect forearm orientation changes with an accelerometer.
- Count a repetition only after a complete
  `start pose -> target pose -> start pose` cycle.
- Compare later repetitions with the first valid movement.
- Cycle heart rate, repetition, and movement information on one OLED.
- View live data through a Windows/macOS local dashboard.
- View data over the local network or through the Yun Wi-Fi dashboard.
- Support ADXL345, BMA150, and BMX160 accelerometers.

## Required Hardware

- Arduino UNO R4 Minima
- Dragino Yun shield (optional for Wi-Fi operation)
- MAX30100 or MAX30102/05 heart-rate sensor
- ADXL345, BMA150, or BMX160 accelerometer
- Digital or capacitive touch sensor
- 128x64 SSD1306/SSD1315-compatible I2C OLED
- Correctly rated power supply for the complete board stack

See [START_HERE.md](START_HERE.md) and `wiring.pdf` for detailed wiring.

## Important Wiring Notes

Power off the board before changing any wiring. Confirm the voltage rating
printed on every sensor module.

For the common seven-pin BMX160 module:

| BMX160 pin | UNO R4 Minima |
|---|---|
| VCC | 3.3V only |
| GND | GND |
| SDA | A2 |
| SCL | A3 |
| CSB | 3.3V |
| INT1 | Not connected |
| INT2 | Not connected |

The BMX160 responds at I2C address `0x68` with chip ID `0xD8`. It has no
exposed SDO pin, which is expected for this breakout.

## OLED Pages

### Page 1: Heart Rate

Shows the estimated BPM, pulse-acquisition state, raw infrared signal, and a
warning when movement may disturb the optical reading.

### Page 2: Repetitions

Shows completed repetitions, the goal, session state, calibration prompts,
the saved movement span, and elapsed time.

### Page 3: Movement

Shows the current angle relative to the saved start pose, movement feedback,
the duration of the last repetition, and the stored reference duration.

The OLED changes page every 4 seconds. During pose calibration it remains on
the repetition page so the setup prompt stays visible.

## How to Use

1. Secure the accelerometer firmly on the moving forearm.
2. Power the board and wait for the OLED to show the normal pages.
3. Tap and release the touch sensor to start setup.
4. Hold the start pose still for about one second, then tap the touch sensor.
5. Move the arm to the target pose and hold still for about one second.
6. Tap the touch sensor to save the target pose.
7. The two poses must differ by at least 25 degrees. If
   `NEED 25 DEG CHANGE` appears, move farther and save the target again.
8. Tap once more when `READY / TOUCH TO START` appears.
9. Perform `start pose -> target pose -> start pose`.
10. Pause briefly at the start and target positions so each endpoint is
    recognized.

A full cycle counts as one repetition. Tap during a running set to pause or
resume. Hold the touch sensor for about two seconds to clear calibration.

## Windows Quick Start

### 1. Install Arduino Dependencies

In Arduino IDE:

1. Install **Arduino UNO R4 Boards** from Boards Manager.
2. Install **SparkFun MAX3010x Pulse and Proximity Sensor Library** from
   Library Manager.
3. Open `R4_Health/R4_Health.ino`.
4. Select **Arduino UNO R4 Minima** and the correct COM port.
5. Click **Verify**, then **Upload**.
6. Close the Arduino IDE Serial Monitor after uploading.

The Serial Monitor must stay closed while the local checker is running,
because Windows allows only one program to open the COM port at a time.

### 2. Start the Checker

Double-click:

```text
windows/Run_Health_Checker.bat
```

The first run installs the Python `pyserial` package if necessary. The local
dashboard opens at:

```text
http://127.0.0.1:8770/
```

If you prefer PowerShell:

```powershell
cd "$env:USERPROFILE\Desktop\Health_Project"
py -3 mac\check_health.py --list-ports
py -3 mac\check_health.py --usb COM3
```

Replace `COM3` with the actual Arduino port.

## View from a Phone

When the phone and computer are on the same network, run:

```text
windows/Run_Phone_Viewer.bat
```

Use the network address printed in the command window, for example:

```text
http://192.168.1.25:8770/
```

Do not open `127.0.0.1` on the phone; that address refers to the phone itself.
Some public school or campus networks block communication between connected
devices. In that case, connect the computer to a phone hotspot or use
Tailscale so both devices can communicate securely from different networks.

## Yun Wi-Fi Operation

Install the dashboard and collector on the Yun over SSH:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\install_to_yun.ps1 -YunHost 192.168.240.1
```

Replace the IP with the actual Yun address. Then open:

```text
http://YUN_IP/workout/
```

No public port forwarding is required. Do not expose the unauthenticated Yun
dashboard directly to the public internet.

Older Dragino OpenWrt firmware may require the legacy
`diffie-hellman-group14-sha1` SSH key exchange. The supplied Windows installer
enables that compatibility algorithm only for its SSH/SCP commands.

## GitHub and Group Workflow

Repository:

https://github.com/ning8726/health-project

The repository owner can invite group members through:

1. **Settings**
2. **Collaborators and teams**
3. **Add people**
4. Enter each member's GitHub username.
5. Give normal contributors **Write** access.

Each member should clone the repository:

```powershell
git clone https://github.com/ning8726/health-project.git
cd health-project
```

Create a personal branch before editing:

```powershell
git switch main
git pull
git switch -c feature/member-name-change
```

Commit and upload the branch:

```powershell
git add .
git commit -m "Describe the change"
git push -u origin feature/member-name-change
```

Open a Pull Request on GitHub and let the project owner review it before it
is merged into `main`.

See [GITHUB_GROUP.md](GITHUB_GROUP.md) for the complete group workflow.

## Documentation

- [START_HERE.md](START_HERE.md): complete wiring, upload, Yun, and operation guide
- [WINDOWS_AND_GIT.md](WINDOWS_AND_GIT.md): Windows, phone access, Git, and GitHub
- [GITHUB_GROUP.md](GITHUB_GROUP.md): collaborator and Pull Request workflow
- [VALIDATION.md](VALIDATION.md): completed tests and remaining hardware checks

## Project Status

- Arduino UNO R4 Minima firmware compiles successfully.
- Windows serial and browser dashboard operation have been verified.
- BMX160 hardware was detected at address `0x68` with chip ID `0xD8`.
- Heart-rate and movement values are prototype estimates.
- Tilt calibration and movement sessions require physical hardware checks.
