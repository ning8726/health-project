# Validation

Windows update:

- The sketch compiled for `arduino:renesas_uno:minima` with Arduino UNO R4
  Boards 1.6.0 and SparkFun MAX3010x library 1.1.2.
- A host mock tested BMA150 chip-ID detection, `CSB`-selected address `0x38`,
  protected configuration bits, and 10-bit signed acceleration decoding.
- The Windows checker served the dashboard and JSON through `127.0.0.1`;
  `pyserial` port enumeration worked.
- The Windows batch launcher and PowerShell Yun installer passed syntax checks.
- BMA150 physical I2C timing, the 7-pin module's pull-ups, and actual readings
  have not been tested on the hardware in this workspace.

Software checks completed in the available Linux workspace:

- The complete Arduino sketch compiled as C++11 against a host Arduino API
  substitute with `-Wall -Wextra -Wformat=2 -Werror`.
- AddressSanitizer and UndefinedBehaviorSanitizer checks passed. Leak checking
  was disabled because this execution environment uses ptrace.
- Synthetic pose tests covered completed and partial repetitions, endpoint
  holds, data loss, pause/resume, cycle timeout, goal completion, insufficient
  angular span, non-finite input, page cycling and millisecond rollover.
- Tests checked stationary pose calibration, display-frame bounds and decimal
  formatting without relying on embedded printf floating-point support.
- Six Python tests passed: strict protocol/numeric validation, fragmented and
  oversized serial lines, stale data, frozen Wi-Fi JSON, pseudo-terminal USB
  reception/disconnect, and JSON produced by the host-compiled firmware.
- Python files compiled, and shell installers passed shell syntax checks.
- The Mac checker HTTP server returned simulated JSON and served its dashboard.
  Browser rendering was not checked: the installed Chromium launcher has no
  matching browser binary in this workspace. The dashboard JavaScript passed
  a syntax check.

These are software checks, not measurements from your physical hardware.
The host substitute does not model the ADC, USB controller, I2C timing,
optical sensor, or accelerometer. Arduino CLI / the R4 board toolchain was
not available here, so an actual Arduino IDE Verify and Upload is still needed.

The collector is written for Python 2.7 / 3; it was executed under Python 3.
Python 2.7, the actual Dragino firmware, macOS, its USB driver, Wi-Fi reachability,
power, sensor voltage ratings, board-specific pull-ups and end-to-end physical
operation have not been verified in this workspace.

Before your demonstration:

1. Check module labels and wiring with power off; verify the intended supply ratings.
2. Verify/upload using the UNO R4 Minima board selection in Arduino IDE.
3. Start the direct-USB Mac checker with Serial Monitor closed.
4. Confirm one OLED is detected and each sensor reports fresh data.
5. Check stationary calibration, incomplete movements and full repetitions.
6. Check pulse contact and acquisition without presenting the BPM as validated.
7. Install the Yun collector, move the USB data cable to its host port,
   and confirm advancing packet sequence numbers over Wi-Fi.
8. Unplug a sensor or the USB cable and confirm the checker clears stale values.

To repeat the software tests from this folder on a compatible POSIX system:

```sh
g++ -std=c++11 -Wall -Wextra -Wformat=2 -Werror -I tests tests/test_firmware.cpp -o /tmp/health-firmware-test
/tmp/health-firmware-test
FIRMWARE_TEST_BINARY=/tmp/health-firmware-test python3 tests/test_software.py
```

`tests/Arduino.h` and `tests/heartRate.h` are only host-test substitutes.
Do not copy those files into the Arduino sketch folder.
