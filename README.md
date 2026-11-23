# Petkit Element Solo ESPHome Firmware

**Custom.** A replacement firmware for the PETKIT Fresh Element Solo (D4) cat feeder, with native Home Assistant integration.

It feeds on its own schedule, offline and without Home Assistant, using the on-board RTC. Motor control, jam handling, sensors, buttons, sounds and power checks follow the stock firmware, as recovered by analysing it (see [Documentation](#documentation)). It started from a community ESPHome config and fixes several of its issues (see [docs/motion.md](docs/motion.md#changes-from-the-community-config)).

**Installing:**
1. Copy `secrets.yaml.sample` to `secrets.yaml` and fill it in.
2. Back up the stock flash first (4 MB).
3. Flash `petkit-element-solo.yaml` over serial.
4. After that, update it over the air.

**Several feeders:** `petkit-element-solo.yaml` is the shared firmware. For each extra feeder, add a small file like [`symba-feeder.yaml`](symba-feeder.yaml). It sets the name and points the API key and OTA password at that feeder's own secrets, then includes the shared firmware.


## Features

Behaves like the stock firmware:
- Counts loops on the grate switch like stock: once per closure, re-armed after 300 ms, plus one extra loop when the outlet is open at start ([details](docs/motion.md#counting-loops))
- Each motor run has the stock timeout, (loops + 1) × 10 s + 5 s ([details](docs/motion.md#one-motor-run))
- Detects stalls from the motor current: above 799 for more than 150 ms, ignoring the first 500 ms ([details](docs/motion.md#one-motor-run))
- Clears jams automatically: reverses 1 or 2 loops, up to 5 attempts, then finishes the missing loops ([details](docs/motion.md#over-current-the-un-jam-sequence))
- Reports a timeout as the stock `GRAT_ERR` or `BLOCK_FOOD`, judged against a current of 700 ([details](docs/motion.md#timeout-grate-error-or-food-blocked))
- Counts only loops that dropped food (outlet IR), and detects a blocked outlet as `BLOCK_DOOR` ([details](docs/motion.md#food-at-the-outlet-gpio14))
- Detects low food with the hopper IR, with the stock alarm: 5 beeps every 30 s ([details](docs/power.md#food-level))
- Button like stock: click cancels, hold about 1.5 s to feed while held, child lock ([details](docs/hmi.md#buttons))
- Cancels like stock: the outlet is always left closed ([details](docs/motion.md#cancelling))
- Refuses to feed on a power fault, or with batteries at or below 5.5 V ([details](docs/power.md))
- Stock LED and beep patterns ([details](docs/hmi.md))
- Keeps time on the PCF8563 RTC and keeps feeding when Wi-Fi is down ([details](docs/rtc-and-time.md))

Extra features:
- 24 hourly schedule slots that can be set from Home Assistant and are saved on the device ([details](docs/schedule.md#esphome-behaviour))
- Home Assistant events:
  - `esphome.feeder_food_dispensed`
  - `esphome.feeder_food_low`
  - `esphome.feeder_fault` (`GRAT_ERR`, `BLOCK_FOOD`, `BLOCK_DOOR`)
- `Dispense food` and `Cancel feed` buttons
- Diagnostics: motor fault, last feed max current, un-jam attempts, DC and battery voltages, power fault, low battery, on battery


## Motion

A feed of N scoops is N grate loops, plus one if the outlet was open. Each loop is counted on the falling edge of GPIO27.

**Jam.** If the current stays above 799 for more than 150 ms, the motor brakes and pauses 500 ms. It then turns the other way for 1 loop, or 2 if the previous attempt started less than 3 s earlier. After that, it goes forward again for the loops still due. After 5 attempts it gives up with `BLOCK_FOOD`.

**Timeout.** If a run times out, the result depends on the highest current of the feed:
- below 700: `GRAT_ERR`;
- 700 or more: `BLOCK_FOOD`.

| Command | GPIO19 | GPIO17 | GPIO18 |
|---|---|---|---|
| Forward | 1 | 0 | 1 |
| Reverse | 1 | 1 | 0 |
| Brake | 1 | 1 | 1 |
| Driver asleep | 0 | x | x |

The state machine is in [`feeder_motor.h`](feeder_motor.h), with no ESPHome dependency. Its tests run on a computer:

```bash
cd test && c++ -std=c++17 -Wall -Wextra -I.. feeder_motor_test.cpp -o /tmp/feeder_motor_test && /tmp/feeder_motor_test
```


## Time and offline feeding

At boot, the system clock is loaded from the RTC. Every sync, from Home Assistant or SNTP (with public fallbacks), writes it back. Wi-Fi and API losses never cause a reboot.

The stock firmware stored local time in the RTC, while ESPHome stores UTC. So until the first SNTP sync after flashing, the clock is off by the stock offset ([details](docs/rtc-and-time.md#esphome-behaviour)).


## Documentation

- [docs/hardware.md](docs/hardware.md): pinout, H-bridge, motor current, supply ADCs, GPIO23
- [docs/motion.md](docs/motion.md): loop counting, outlet IR, motor runs, un-jam, replenish, cancel, hold-to-feed, and the ESPHome state machine
- [docs/hmi.md](docs/hmi.md): buttons and their timings, LED styles, beep types
- [docs/power.md](docs/power.md): DC and battery detection, power fault, low battery, hopper IR and the food-low alarm
- [docs/rtc-and-time.md](docs/rtc-and-time.md): how the stock firmware uses the RTC, and how ESPHome does
- [docs/schedule.md](docs/schedule.md): the stock schedule format, offline triggering and battery sleep

These pages describe behaviour in our own words and contain no code from the stock firmware.


## Roadmap

- [ ] Check on the device:
  - [ ] the 3-tick `BLOCK_DOOR` rule with a real bowl
- [ ] SET button long press: start an ESPHome Wi-Fi setup mode (stock: network pairing)
- [ ] Light sleep on batteries (stock: wakes at least every 9 min, and 30 s before a feed)
- [ ] Schedules to the minute with repeat days
