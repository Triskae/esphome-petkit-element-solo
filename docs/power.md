# Power and food level

## Power source

The feeder runs from a DC adapter or from batteries:

- **GPIO21 high** means running on batteries;
- **GPIO39** measures the DC input voltage;
- **GPIO36** measures the battery voltage.

Both voltages sit behind a 147/47 divider (see [hardware.md](hardware.md#supply-voltages)).

On batteries, the battery reading is the **highest value seen in the last 30 minutes**, so motor dips are ignored. ESPHome does the same, with a 30-sample `max` filter at one reading a minute.

### Power fault (`DC_ERR`)

The stock firmware raises `DC_ERR` when the condition below lasts more than **4 s**:

| Source | Fault when |
|---|---|
| DC adapter | DC input below 4.5 V or above 7.0 V |
| Batteries | DC input above 7.0 V |

A feed does not start while `DC_ERR` is set. ESPHome exposes it as `Power fault` and refuses feeds the same way.

### Low battery

On batteries, a feed is refused when the battery is at or below **5.499 V**. ESPHome exposes it as `Low battery` and refuses feeds the same way.

### Battery-only behaviour (stock)

Only on batteries, the stock firmware:
- reads the hopper IR in power-saving mode, on the variant that uses it (see below);
- does not sound the food-low alarm;
- accepts the "ship mode" button combination.

It also uses light sleep between feeds:
- it does not sleep if the next feed is 30 s away or less;
- it sleeps until 30 s before the feed when that feed is 31–570 s away;
- otherwise it sleeps 540 s;
- it wakes on the timer and on GPIO34, GPIO0 and GPIO21.

ESPHome does not implement light sleep: on batteries, Wi-Fi stays on and drains them.

## Food level

**Where "food low" comes from.** On the tested units, the stock food state is set by the **feed results**, not by a hopper sensor. At the end of a feed (not cancelled, outlet not blocked), the app task logs `app result over food empty!` and sets the state to **empty** when no food was seen at the outlet (`valid < 1`), or when fewer scoops than requested were seen and an internal flag is not set. Otherwise it sets the state back to **normal**. So refilling the hopper does not stop the alarm: the next feed that sees food does.

The state is saved, so it survives a reboot. Each change is reported to the cloud.

**Hopper IR (one hardware variant only).** The firmware also has an IR beam across the hopper: emitter on **GPIO2**, receiver on **GPIO13**, with the internal pull-up (same `gpio_config` call as GPIO14, GPIO26 and GPIO27). It only polls it when a 2-character code in its stored config selects hardware variant `0xe5`:

| Mode | How it reads |
|---|---|
| Normal (on DC) | Emitter always on; a new state needs 3 identical reads in a row. |
| Power saving (on batteries) | Every second: emitter on, wait 50 ms, read; a new state needs 2 identical reads; emitter off. |

On the two tested units, nothing drives GPIO13: it reads 0 without the pull-up and 1 with it, whether the hopper is full or empty, and it does not react when the emitter (GPIO2) or the sensor power (GPIO33) is switched off. These units are not that variant, or have no hopper sensor fitted.

**Food-low alarm.** While the food state is empty, the firmware plays **5 beeps of 500 ms on / 500 ms off every 30 s**, under these conditions:
- the food-warning switch is on;
- the time is inside the configured warning range;
- the feeder is not on batteries.

ESPHome:
- sets `Food low` when a feed ends with no food seen at the outlet, and clears it when a feed sees food (kept across reboots);
- does not use the partial-feed rule yet, because the flag it depends on is not identified;
- sends `esphome.feeder_food_low` when a feed sees no food;
- keeps the hopper emitter on and reads GPIO13 as an internal input (`hopper_ir`), unused;
- plays the stock alarm every 30 s while `Food low alarm` is on, between `Food alarm from hour` and `Food alarm to hour`, and not on batteries.

Earlier versions took `Food low` from GPIO13. Without the pull-up it never turned on; with it, it never turned off.
