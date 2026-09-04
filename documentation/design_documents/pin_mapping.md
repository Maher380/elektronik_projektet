# Arduino Nano ESP32: Code-To-Pin Reference

The driver factory uses the **ESP32-S3 GPIO number**, not the Arduino pin
number printed on the board.

> `factory.gpioOutput(2)` selects **GPIO2 = A1**, not Arduino pin D2.
> To select the board pin **D2**, use `factory.gpioOutput(5)` or
> `factory.pwm(5)`.

The `~` symbol marks a PWM-capable Arduino pin. The symbol is only a visual
label; it is not written in the C++ call.

## Pins Used By CnB

These are the calls and physical pins used by the current three-sensor code on
`SCRUM-56-fix-adc-support-for-multiple-ir-sensors`.

| Driver call | Pin on Arduino | Driver function | Connected hardware | Status |
| --- | --- | --- | --- | --- |
| `factory.adc(1)` | `A0 / ~D17` | 📈 ADC1_CH0 | Left IR sensor output | 🔵 Used |
| `factory.adc(2)` | `A1 / ~D18` | 📈 ADC1_CH1 | Center IR sensor output | 🔵 Used |
| `factory.adc(4)` | `A3 / ~D20` | 📈 ADC1_CH3 | Right IR sensor output | 🔵 Used |
| `factory.pwm(5)` | `~D2` | 〰️ PWM output | MP6550 IN1, forward | 🔵 Used |
| `factory.pwm(6)` | `~D3` | 〰️ PWM output | MP6550 IN2, reverse | 🔵 Used |
| `factory.gpioOutput(7)` | `~D4` | 🔌 Digital output | MP6550 nSLEEP | 🔵 Used |

Power connections do not use driver factory calls:

| Pin on Arduino | Connection |
| --- | --- |
| `VIN` | Approximately 7.2 V battery supply after the power switch |
| `GND` | Common ground for Arduino, MP6550, regulator, and sensors |
| External regulated 5 V rail | IR sensor power; the demo used an L7805CV |

## Code Value To Board Pin

Use the first column when passing a pin to `gpioInput()`, `gpioOutput()`,
`pwm()`, or `adc()`.

### Legend

| Symbol | Meaning |
| --- | --- |
| 🔵 | Used by the current CnB firmware |
| 🟢 | Available for a future connection |
| 🟡 | Available, but shared with a standard peripheral or onboard function |
| 🔴 | Avoid; blocked by the current pin manager |
| ✅ | Supported by the current driver |
| ⚠️ | Supported by the chip, but not by the current ADC1-only driver |
| `~` | PWM-capable Arduino pin |

| GPIO in code | Pin on Arduino | ADC status | PWM status | Common function | Availability |
| ---: | --- | --- | --- | --- | --- |
| `0` | `B1 / ~D15` | - | 🔴 Blocked | RGB LED green / strapping | 🔴 Avoid |
| `1` | `A0 / ~D17` | ✅ ADC1_CH0 | ✅ | Analog / digital | 🔵 Left IR sensor |
| `2` | `A1 / ~D18` | ✅ ADC1_CH1 | ✅ | Analog / digital | 🔵 Center IR sensor |
| `3` | `A2 / ~D19` | 🔴 ADC1_CH2 blocked | 🔴 Blocked | Strapping | 🔴 Avoid |
| `4` | `A3 / ~D20` | ✅ ADC1_CH3 | ✅ | Analog / digital | 🔵 Right IR sensor |
| `5` | `~D2` | ✅ ADC1_CH4 | ✅ | Digital / PWM | 🔵 MP6550 IN1 |
| `6` | `~D3` | ✅ ADC1_CH5 | ✅ | Digital / PWM | 🔵 MP6550 IN2 |
| `7` | `~D4` | ✅ ADC1_CH6 | ✅ | Digital / PWM | 🔵 MP6550 nSLEEP |
| `8` | `~D5` | ✅ ADC1_CH7 | ✅ | Digital / PWM | 🟢 Available |
| `9` | `~D6` | ✅ ADC1_CH8 | ✅ | Digital / PWM | 🟢 Available |
| `10` | `~D7` | ✅ ADC1_CH9 | ✅ | Digital / PWM | 🟢 Available |
| `11` | `A4 / ~D21` | ⚠️ ADC2_CH0 | ✅ | I2C SDA | 🟡 Shared function |
| `12` | `A5 / ~D22` | ⚠️ ADC2_CH1 | ✅ | I2C SCL | 🟡 Shared function |
| `13` | `A6 / ~D23` | ⚠️ ADC2_CH2 | ✅ | Analog / digital | 🟢 Available |
| `14` | `A7 / ~D24` | ⚠️ ADC2_CH3 | ✅ | Analog / digital | 🟢 Available |
| `17` | `~D8` | ⚠️ ADC2_CH6 | ✅ | Digital / PWM | 🟢 Available |
| `18` | `~D9` | ⚠️ ADC2_CH7 | ✅ | Digital / PWM | 🟢 Available |
| `21` | `~D10` | - | ✅ | Digital / PWM | 🟢 Available |
| `38` | `~D11` | - | ✅ | SPI COPI | 🟡 Shared function |
| `43` | `~D1 / TX0` | - | ✅ | UART transmit | 🟡 Shared function |
| `44` | `~D0 / RX0` | - | ✅ | UART receive | 🟡 Shared function |
| `45` | `~D16` | - | 🔴 Blocked | RGB LED blue / strapping | 🔴 Avoid |
| `46` | `B0 / ~D14` | - | 🔴 Blocked | RGB LED red / strapping | 🔴 Avoid |
| `47` | `~D12` | - | ✅ | SPI CIPO | 🟡 Shared function |
| `48` | `~D13` | - | ✅ | SPI SCK / built-in LED | 🟡 Shared function |

GPIO values not listed above are not exposed as normal Arduino Nano ESP32
header pins. Do not use them only because `PinManager::isPinValid()` returns
true; the current pin manager does not verify that a GPIO is physically exposed
on this board.

## How The Factory Interprets A Number

| Code | Meaning |
| --- | --- |
| `factory.gpioInput(n)` | Configure GPIO `n` as a digital input |
| `factory.gpioOutput(n)` | Configure GPIO `n` as a digital output |
| `factory.pwm(n)` | Route a PWM output to GPIO `n` |
| `factory.adc(n)` | Use GPIO `n` as an ADC1 input if it maps to ADC1 |

For example:

```cpp
auto leftSensor = factory.adc(1);        // GPIO1 -> A0 -> ADC1_CH0
auto motorIn1 = factory.pwm(5);          // GPIO5 -> ~D2
auto motorSleep = factory.gpioOutput(7); // GPIO7 -> ~D4
```

The three IR sensors share one `ADC_UNIT_1` handle and use channels 0, 1, and
3. The ADC2-capable pins shown in the table cannot be initialized by the
current ADC driver because it deliberately accepts ADC1 only.

## Power And Control Pins

| Pin on Arduino | Type | Meaning |
| --- | --- | --- |
| `VIN` | Power input | External supply input for the Nano ESP32 |
| `3V3` | Power output | Regulated 3.3 V output; GPIO is not 5 V tolerant |
| `VUSB` | Power output | USB-derived supply; do not assume it is active from VIN |
| `GND` | Ground | Common electrical reference |
| `RESET` | Control input | Reset when pulled low |

## Sources

- [Arduino Nano ESP32 full pinout](https://docs.arduino.cc/resources/pinouts/ABX00083-full-pinout.pdf)
- [Arduino Nano ESP32 datasheet](https://docs.arduino.cc/resources/datasheets/ABX00083-datasheet.pdf)
- [Espressif ESP32-S3 GPIO reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html)
