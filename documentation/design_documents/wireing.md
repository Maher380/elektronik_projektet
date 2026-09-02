# Demo Wiring

This document records the wiring used for the first CnB vehicle demo. It is
historical release documentation, not the final power-system design.

## Power Distribution

```text
7.2 V battery positive
        |
        +-- power switch --+--> Arduino Nano ESP32 VIN
                            +--> MP6550 VIN/VM
                            +--> L7805CV IN

L7805CV OUT --------------------> IR sensor VCC
7.2 V battery negative --------> Common GND
```

The Arduino, MP6550, L7805CV and IR sensor must share the same ground. The
Arduino VBUS pin was not used to power the IR sensor during the demo.

### L7805CV

| L7805CV pin | Demo connection |
|-------------|-----------------|
| IN | Switched 7.2 V battery supply |
| GND | Common GND |
| OUT | IR sensor red wire (VCC) |

Confirm the physical package orientation against the L7805CV datasheet before
rebuilding the circuit. The KiCad project must record the actual input and
output capacitors used in the demo before the `v1.0-demo` tag is created.

## Arduino Nano ESP32

| Arduino pin | ESP32-S3 GPIO | Demo connection | Signal type | Wire color |
|-------------|---------------|-----------------|-------------|------------|
| GND | GND | Common GND | Ground | Black |
| D2 | GPIO5 | MP6550 IN2 | PWM | Orange |
| D3 | GPIO6 | MP6550 IN1 | PWM | Brown |
| D4 | GPIO7 | MP6550 nSLEEP | Digital output | Gray |
| VIN | VIN | Switched 7.2 V battery supply | Power input | Red |
| A0 | GPIO1 | IR sensor output | ADC input | Yellow |

In the demo firmware, D2/GPIO5 is represented by
`mp6550MotorPwmForwardPin` and D3/GPIO6 by
`mp6550MotorPwmBackwardPin`. The table above preserves the physical demo
wiring to MP6550 IN2 and IN1.

## MP6550 Motor Controller

| MP6550 connection | Demo connection | Wire color |
|-------------------|-----------------|------------|
| GND | Common GND | Black |
| VIN/VM | Switched 7.2 V battery supply | Red |
| OUT1 | Motor terminal 1 | Green |
| OUT2 | Motor terminal 2 | Blue |
| IN1 | Arduino D3 / GPIO6 | Brown |
| IN2 | Arduino D2 / GPIO5 | Orange |
| nSLEEP | Arduino D4 / GPIO7 | Gray |

## IR Distance Sensor

| Sensor wire | Demo connection | Wire color |
|-------------|-----------------|------------|
| Black | Common GND | Black |
| Red | L7805CV OUT | Red |
| Yellow | Arduino A0 / GPIO1 | Yellow |

The IR sensor was powered through the L7805CV during the demo, not directly
from the 7.2 V battery.

## Power Switch and Battery

| Source | Destination | Wire color |
|--------|-------------|------------|
| Battery positive | Power-switch input | Red |
| Power-switch output | Switched 7.2 V supply | Red |
| Battery negative | Common GND | Black |
