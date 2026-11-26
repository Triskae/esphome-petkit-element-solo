# Hardware: pins and peripherals

What each ESP32 pin does on the PETKIT Fresh Element Solo (D4), as established while studying the stock firmware's behaviour for interoperability. The confidence column says how solid each line is:

- **confirmed**: the stock firmware uses the pin this way, without ambiguity;
- **inferred**: the pin number is certain, its physical role is deduced from how it is used.

| Pin | Direction | Role | Confidence | ESPHome |
|---|---|---|---|---|
| GPIO19 | out | Motor driver awake (low = driver asleep) | confirmed | `motor_enable` |
| GPIO17 | out | H-bridge input A | confirmed | `motor_in_a` |
| GPIO18 | out | H-bridge input B | confirmed | `motor_in_b` |
| GPIO32 | ADC1 ch4, 0 dB | Motor current shunt | confirmed | `motor_current_adc` |
| GPIO27 | in, pull-up, interrupt on falling edge | Grate switch: low when the grate closes the outlet, once per loop | confirmed | `motor_sensor` |
| GPIO14 | in, pull-up, interrupt on rising edge | Outlet IR: high when something covers the beam | confirmed | `feed_sensor`, `outlet_covered_idle` |
| GPIO2 | out | Hopper (kibble) IR emitter | confirmed | `kibble_ir_emitter` |
| GPIO13 | in, pull-up | Hopper (kibble) IR receiver, polled by stock on hardware variant `0xe5` only; not driven on the tested units | confirmed | `hopper_ir` (internal, unused) |
| GPIO33 | out | Sensor power (IRs and switch) | confirmed | `enable_sensors`, always on |
| GPIO23 | in, pull-up | "moto fail" line, never read by the stock firmware (see below) | confirmed | not used |
| GPIO16 | out | Buzzer (stock: 2 kHz, 50 % duty) | confirmed | `rtttl_output` |
| GPIO5 | out | Status LED | confirmed | `led` |
| GPIO34 | in | FUNC button (feed / cancel), light-sleep wake source | confirmed | `manual_feed_button` |
| GPIO0 | in | SET button (network pairing), light-sleep wake source | confirmed | `set_button` |
| GPIO21 | in | High = running on batteries | confirmed | `on_battery` |
| GPIO39 | ADC1 ch3, 11 dB | DC input voltage, 147/47 divider | confirmed | `DC input voltage` |
| GPIO36 | ADC1 ch0, 11 dB | Battery voltage, 147/47 divider | confirmed | `Battery voltage` |
| GPIO15 / GPIO4 | I2C | RTC: SDA / SCL, 200 kHz | confirmed | `i2c` |
| GPIO1 / GPIO3 | UART0 | Console | | `uart` (unused) |

## Motor H-bridge

The motor has no speed control: the stock firmware never uses PWM on it. It only uses four pin states:

| Command | GPIO19 | GPIO17 | GPIO18 |
|---|---|---|---|
| Forward (feeding) | 1 | 0 | 1 |
| Reverse (un-jam) | 1 | 1 | 0 |
| Brake | 1 | 1 | 1 |
| Driver asleep | 0 | x | x |

The stock firmware brakes as soon as the last loop is counted: it does this from the grate interrupt itself. It puts the driver to sleep once the motor is idle.

## GPIO23, the "moto fail" line

At start-up, the stock firmware configures GPIO23 as an input with a pull-up. It drives it high while the motor stage sleeps, and makes it an input again on wake. Nothing ever reads it.

The name, together with the pull-up, suggests the motor driver's open-drain fault output. The stock firmware ignores it, and so does ESPHome.

## Motor current

The firmware reads the shunt on ADC1 channel 4 (GPIO32) with 0 dB attenuation. Each read averages 32 samples and goes through the ESP-IDF calibration. The calibrated millivolts are then multiplied by **16 / 11**. All thresholds use that scaled value.

## Supply voltages

Two ADC1 channels, read with 11 dB attenuation, sit behind a 147/47 divider. The firmware multiplies them by 147 / 47 ≈ 3.128 to get millivolts.

- **GPIO39 is the DC input**.
- **GPIO36 is the battery**. On batteries, the firmware keeps the highest reading of each 30-minute window, so that motor dips do not count.

See [power.md](power.md) for the thresholds.

## RTC

The RTC is a PCF8563 (or the compatible BM8563) at I2C address `0x51`. See [rtc-and-time.md](rtc-and-time.md).
