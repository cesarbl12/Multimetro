# ESP32 Digital Multimeter

A 6-in-1 digital multimeter built on an ESP32, controlled either over Serial or from a built-in web interface (WiFi Access Point + WebSocket). Includes per-mode linear-interpolation calibration, editable live from the web UI and persisted to flash (NVS).

![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B%20(Arduino)-00979D)
![Status](https://img.shields.io/badge/status-active%20development-yellow)

## Features

| # | Mode | Range | Method |
|---|------|-------|--------|
| 1 | Voltage | 0–20 V | [Adafruit INA219](https://www.adafruit.com/product/904) over I2C |
| 2 | Capacitance | — | Timed RC charge/discharge |
| 3 | Continuity | — | Digital probe + buzzer |
| 4 | Resistance (Ohmmeter) | 0–100 kΩ | Auto-ranging divider, 4 scales (100 Ω / 1 kΩ / 10 kΩ / 100 kΩ) |
| 5 | Frequency | up to 10 kHz | Reciprocal edge counting |
| 6 | Current (Amperimeter) | — | Low-side 1 Ω shunt + Ohm's law |

- **Dual control surface**: the Serial monitor and the web page drive the exact same command interpreter and state machine, so both stay in sync — switching modes ('1'–'6') from anywhere cancels whatever was running and starts the new mode automatically.
- **Live "oscilloscope" view** for Frequency mode: the browser draws a synthesized square wave from the measured Hz (GPIO25 is an ADC2 pin, which isn't reliable for real waveform sampling while WiFi is active — see comments in the sketch).
- **On-device web server**: self-hosted WiFi Access Point (`http://192.168.4.1` or `http://multimetro.local`), no external network or app required.
- **Calibration built into the UI**: each numeric mode (all but Continuity) exposes its live raw reading plus "Add point" / "Reset table" controls, backed by a small hand-rolled JSON protocol over WebSocket.

## Web Interface

Connect a phone or laptop to the ESP32's own WiFi network and open the browser to `http://192.168.4.1` (or `http://multimetro.local` via mDNS). The UI is styled to look like a real handheld multimeter — dial-style mode buttons, an LCD-style green digit display, and decorative probe jacks — and includes the live calibration panel described above.

> Default AP credentials are set in the sketch (`AP_SSID` / `AP_PASSWORD`). Change them before deploying outside a lab bench.

## Architecture

- **Firmware**: single Arduino sketch (`multimetro_esp32/multimetro_esp32.ino`), synchronous `WebServer` (HTTP, port 80) + [`WebSocketsServer`](https://github.com/Links2004/arduinoWebSockets) (port 81). This combination replaced an earlier `ESPAsyncWebServer` + `AsyncTCP` implementation that hit persistent lwIP locking crashes on ESP32 core 3.3.0 / IDF5.
- **Calibration**: a generic `TablaCal` struct + `Preferences` (NVS) backs 8 independent calibration tables (Voltage, Capacitance, 4 Ohmmeter ranges, Frequency, Current), each starting empty (identity mapping) until the user adds real reference points from the web panel. Linear interpolation (`interpolarLineal()`) is shared across all 5 numeric modes.
- **State machine**: `comandoPendiente()` / `procesarComando()` handle both Serial and WebSocket input identically.

## Hardware / Pinout

Each mode routes the positive probe through its own relay (driven via an NPN transistor + flyback diode — **never drive a relay coil directly from a GPIO**).

| Mode | Relay GPIO | Sense pin(s) | Notes |
|------|-----------:|--------------|-------|
| Voltage | 23 | I2C: SDA 16 / SCL 21 | Relay connects probe+ to INA219 `VIN-`; module read via I2C |
| Capacitance | 22 | 36 (analog), 32 (charge), 33 (discharge) | RC pair, ~10 kΩ reference resistor |
| Continuity | 9 | 4 (`PROBE_PIN`, pull-up) | Buzzer on GPIO 5 (active-high) |
| Resistance | 19 (+ aux relay 10) | 35 (analog) | Reference channels: 13 (100 Ω), 14 (1 kΩ), 26 (10 kΩ), 27 (100 kΩ) |
| Frequency | 18 | 25 | 1 kΩ/10 kΩ divider, no comparator — expects a square-wave source up to 3.3 V / 10 kHz |
| Current | 17 | 39 (analog) | 1 Ω / 1 W low-side shunt — rate the relay for your expected load current |

Full wiring rationale, gotchas, and why each design choice was made live as inline comments at the top of [`multimetro_esp32.ino`](multimetro_esp32/multimetro_esp32.ino).

## Getting Started

1. Open `multimetro_esp32/multimetro_esp32.ino` in the Arduino IDE (board: **ESP32 Dev Module**).
2. Install the required libraries via Library Manager: `Adafruit INA219`, `WebSockets` (Links2004), plus the bundled ESP32 core libraries (`WiFi`, `ESPmDNS`, `WebServer`, `Preferences`, `Wire`).
3. Wire the circuit per the pinout table above.
4. Flash the board, then either:
   - open the Serial Monitor (115200 baud) and use the text menu, or
   - connect to the `Multi` WiFi network and browse to `http://192.168.4.1`.

## Calibration

Every numeric mode ships with an empty (identity) calibration table. To reach <2% error:

1. Apply a known reference value using an instrument noticeably more accurate than the target error.
2. Note the live "raw reading" shown in the web calibration panel.
3. Enter the real value and press **Add point**.
4. Repeat across several points spread over the full range (for multi-decade modes like Capacitance, Resistance, and Frequency, spread points logarithmically — one per decade).
5. Resistance has 4 independent tables, one per range — calibrate each separately.
6. Verify with control points that weren't used for calibration.

## Project Structure

```
multimetro_esp32/
└── multimetro_esp32.ino   # firmware: state machine, web server, calibration, all 6 modes
```

## Branches

- `master` — stable, Serial-only control (no web UI).
- `V2-GUI` — active development branch: full web interface + NVS-backed calibration.

## Known Limitations

- Frequency mode's calibration strategy is still being finalized.
- ADC2 (GPIO25) can't be reliably read while WiFi is active, so Frequency mode never samples a real waveform — see the "oscilloscope" note above.
- Calibration tables start empty and need to be populated per-device before readings are trustworthy.
