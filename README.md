# Petkit Element Solo ESPHome Firmware

**Custom.** A replacement firmware for the PETKIT Fresh Element Solo (D4) cat feeder, with native Home Assistant integration.

It feeds on its own schedule, offline and without Home Assistant, using the on-board RTC. Motor control, jam handling, sensors, buttons, sounds and power checks follow the stock firmware's behaviour, studied on a device owned by the author for interoperability purposes (see [Documentation](#documentation)). It started from a community ESPHome config and fixes several of its issues (see [docs/motion.md](docs/motion.md#changes-from-the-community-config)).

## Quick start

You need ESPHome (the Home Assistant add-on / Device Builder, or the `esphome` command line) and, for the first flash only, a USB-serial adapter wired to the feeder's board.

1. **Open the feeder and wire the serial adapter.** The board, the solder pads and the boot-mode button are shown on the [devices.esphome.io page](https://devices.esphome.io/devices/Petkit-Fresh-Element-Solo-Pet-Feeder/).
2. **Back up the stock firmware** before anything else, so you can go back:
   ```bash
   esptool.py --baud 115200 read_flash 0 ALL petkit-stock-backup.bin
   ```
   Some boards have 8 MB of flash instead of 4 MB; `ALL` reads the whole chip either way.
3. **Copy [`petkit-feeder.yaml`](petkit-feeder.yaml)** into your ESPHome Device Builder (or next to your `secrets.yaml`).
   - Set `name`, `friendly_name` and `timezone`.
   - Fill `wifi_ssid`, `wifi_password`, `api_encryption_key` and `ota_password` in your `secrets.yaml`. See [`secrets.yaml.sample`](secrets.yaml.sample).

   The hardware (`config.yaml`), the feeding logic (`feeder.yaml`) and the motor controller (`components/feeder_motor`) are all pulled from this repository.
4. **Flash it over serial once:**
   ```bash
   esphome run petkit-feeder.yaml
   ```
   Pick the USB port. Every later update goes over Wi-Fi.
5. **Add the feeder in Home Assistant** (it is discovered automatically), and set the hourly scoops.

**What's in the repository:**

| File | Role |
|---|---|
| [`petkit-feeder.yaml`](petkit-feeder.yaml) | Ready-to-use config for your own feeder |
| [`config.yaml`](config.yaml) | Hardware only: pins, sensors, motor outputs, RTC |
| [`feeder.yaml`](feeder.yaml) | Feeding logic, entities and Home Assistant events |
| [`components/feeder_motor`](components/feeder_motor) | Motor controller (`external_component`) |
| `hella-feeder.yaml`, `symba-feeder.yaml` | The author's two feeders, using the local copy of the files: examples for several feeders |

**Several feeders:** use one `petkit-feeder.yaml` copy per feeder, each with its own `name`. Each copy can point its API key and OTA password at its own secrets, as [`symba-feeder.yaml`](symba-feeder.yaml) does.

**Coming from the community config:** the entity names are the same (hourly cups, `Dispense food`, `Manual dispense scoops`, ...). Your schedule is kept when you flash over the air. The `Low food threshold` entity is gone: food low now comes from the hopper sensor. You can remove the old entity from Home Assistant.


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

The state machine is in [`components/feeder_motor/feeder_motor.h`](components/feeder_motor/feeder_motor.h), with no ESPHome dependency. Its tests run on a computer:

```bash
cd test && c++ -std=c++17 -Wall -Wextra -I../components/feeder_motor feeder_motor_test.cpp -o /tmp/feeder_motor_test && /tmp/feeder_motor_test
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

These pages describe the device's behaviour in our own words. They contain no code, binaries or other material from the stock firmware.


## Roadmap

- [ ] Check on the device:
  - [ ] the 3-tick `BLOCK_DOOR` rule with a real bowl
- [ ] SET button long press: start an ESPHome Wi-Fi setup mode (stock: network pairing)
- [ ] Light sleep on batteries (stock: wakes at least every 9 min, and 30 s before a feed)
- [ ] Schedules to the minute with repeat days


## Disclaimer

This is an independent, unofficial community project. It is **not affiliated with, endorsed by, sponsored by or supported by PETKIT** or any of its subsidiaries or partners.

PETKIT, Fresh Element, Fresh Element Solo and the related names, logos and product designs are trademarks or registered trademarks of their respective owners. They are used here only to identify the hardware this firmware is meant for (nominative use). No affiliation is implied.

This repository contains **no PETKIT code, firmware, binaries, keys or other proprietary material**. The behaviour of the original firmware was studied on a device owned by the author, for the sole purpose of interoperability: running the device with open-source software and controlling it locally. The documentation describes that behaviour in the author's own words. All code here is original.

**Use at your own risk.** Opening the device, flashing a custom firmware or modifying the hardware will most likely void your warranty, and may damage the device or harm the animals relying on it. Always keep a backup of the original firmware and check that your pet is fed. The software is provided "as is", without warranty of any kind, and the authors accept no liability for any damage or loss arising from its use.

If you are a rights holder and believe something here infringes your rights, please open an issue and it will be looked at promptly.

