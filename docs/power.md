# Power and food level

## Power source

The feeder runs from a DC adapter or from batteries:

- **GPIO21 high** means running on batteries;
- **GPIO39** measures the DC input voltage (`dc_mv`);
- **GPIO36** measures the battery voltage (`bat_mv`).

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

On batteries, a feed is refused when the battery is at or below **5.499 V** (`app self check, low energy`). ESPHome exposes it as `Low battery` and refuses feeds the same way.

### Battery-only behaviour (stock)

Only on batteries, the stock firmware:
- reads the hopper IR in power-saving mode (see below);
- does not sound the food-low alarm;
- accepts the "ship mode" button combination.

It also uses light sleep between feeds:
- it does not sleep if the next feed is 30 s away or less;
- it sleeps until 30 s before the feed when that feed is 31–570 s away;
- otherwise it sleeps 540 s;
- it wakes on the timer and on GPIO34, GPIO0 and GPIO21.

ESPHome does not implement light sleep: on batteries, Wi-Fi stays on and drains them.

## Food level

An IR beam crosses the hopper: emitter on **GPIO2**, receiver on **GPIO13**.

**Reading the beam.** The stock firmware reads it in one of two modes:

| Mode | How it reads |
|---|---|
| Normal (on DC) | Emitter always on; a new state needs 3 identical reads in a row. |
| Power saving (on batteries) | Every second: emitter on, wait 50 ms, read; a new state needs 2 identical reads; emitter off. |

**Food state.**
- **GPIO13 = 0** means food is present: a full hopper reads 0, checked on the device. The firmware calls this food state 2.
- **GPIO13 = 1** means the food is low: food state 1.

Each change is reported to the cloud (`need msg report, reason: food_sta`).

**Food-low alarm.** While the food is low, the firmware plays **5 beeps of 500 ms on / 500 ms off every 30 s**, under these conditions:
- the food-warning switch is on;
- the time is inside the configured warning range;
- the feeder is not on batteries.

ESPHome:
- keeps the emitter on;
- exposes `Food low`, which is GPIO13 as read (high = low food), with a 30 ms filter;
- sends `esphome.feeder_food_low` when it turns on;
- plays the stock alarm every 30 s while `Food low alarm` is on, between `Food alarm from hour` and `Food alarm to hour`, and not on batteries.

The first version read this the other way round, deduced from the code alone; a test with a full hopper settled it.
