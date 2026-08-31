# Wiring Diagram

## Vagrant
### ESP32

| Pin | Connection | Color |
|-----|-----------|-------|
| GND | Common GND | Black |
| D2 | MC6550 IN1 | Orange |
| D3 | MC6550 IN2 | Brown |
| D4 | MC6550 !SLP | Gray |
| VIN | Common battery | Red |
| A0 | IR Sensor | Yellow |

### MC6550

| Pin | Connection | Color |
|-----|-----------|-------|
| GND | Common GND | Black |
| VIN | Common battery | Red |
| OUT1 | Forward Engine | Green |
| OUT2 | Backward Engine | Blue |
| IN1 | D2 (ESP32) | Orange |
| IN2 | D3 (ESP32) | Brown |
| !SLP | D4 (ESP32) | Gray |

### IR Sensor

| Wire | Connection | Color |
|-----|-----------|-------|
| Black wire | Common GND | Black |
| Red wire | Common battery | Red |
| Yellow wire | A0 (ESP32) | Yellow |

### Power Switch

| Connection | Link | Color |
|-----------|------|-------|
| Red wire 1 | Common battery | Red |
| Red wire 2 | Battery voltage | Orange |

### Battery

| Terminal | Connection | Color |
|---------|-----------|-------|
| Black | Common ground | Black |
| Red | Power switch | Red |
