# Feeding motion and jam handling

This page covers three things:

- how the stock firmware turns the dispenser, and how it notices a jam and clears it;
- how this ESPHome firmware reproduces that behaviour;
- the few places where ESPHome had to make its own choices.

The logic lives in [`components/feeder_motor/feeder_motor.h`](../components/feeder_motor/feeder_motor.h). It contains no ESPHome code, and [`test/feeder_motor_test.cpp`](../test/feeder_motor_test.cpp) tests it on a computer.

## Loops and portions

The stock app asks for an amount in **grams**. The firmware turns it into a number of grate **loops**: grams ÷ grams-per-loop. Grams-per-loop is a device setting from 2 to 50 g, 10 g by default.

ESPHome skips grams and works in loops directly: one "scoop" is one loop.

## Counting loops

**What counts as a loop.** A loop is counted on the **falling edge of GPIO27**, the moment the grate closes the outlet. The stock firmware counts it in an interrupt, and only while the motor runs.

**One count per closure.** After counting, the firmware sets a "closed" latch and ignores further edges. The latch follows the polled level of GPIO27 again only **300 ms** after the last counted loop. So switch bounce never counts twice: a new loop needs the switch to reopen and then close again.

**Stopping on time.** When the last loop is counted, the interrupt brakes the motor straight away.

**At rest.** While the motor is idle, the GPIO27 state is sampled every 500 ms. It must stay stable for 2 s before it counts as "closed" or "open".

**Outlet open at start.** If the outlet is open when a feed starts, the firmware asks for **one extra loop**. The first closure only closes the outlet; it does not complete a portion.

ESPHome does the same.
- The motor loop runs every 10 ms and polls GPIO27.
- `on_grate_edge()` counts once per closure, with the same 300 ms re-latch and the 2 s idle filter.

## Food at the outlet (GPIO14)

GPIO14 is an IR beam across the outlet; it reads **high when covered**. During a feed, the stock firmware does two things with it:

- **Rising edges** (something crosses the beam) are counted per loop. A loop with **no edge** is a loop that dropped no food. At the end of the feed, only loops with food count towards the reported amount.
- **Uncovered ticks**: the number of 10 ms ticks with the beam free is counted per loop. If the last loop had **fewer than 3**, the outlet stayed covered. The cause is food piled up to the outlet, or a full bowl. The firmware then raises **`BLOCK_DOOR`** and counts every loop as valid.

At rest, the beam is sampled every 500 ms:
- covered for 2 s sets `BLOCK_DOOR`;
- free for 1 s clears it.

ESPHome:
- `Dispensed food scoops` is the number of loops with food, as stock reports it.
- `Dispensed food quantity` is the raw count of GPIO14 edges.
- `Outlet blocked` reflects `BLOCK_DOOR`, and an `esphome.feeder_fault` event with `fault: BLOCK_DOOR` is sent when a feed ends with the outlet blocked.

## One motor run

Each motor run asks for N loops, with a timeout of **(N + 1) × 10 s + 5 s**. While it runs, the driver checks the following:

| Check | Rule |
|---|---|
| Loops done | All N counted: normal end (the grate interrupt already braked). |
| Timeout | The run stops and the feed is judged (see below). |
| Over-current | Ignored for the first **500 ms** (inrush). After that, a current **above 799 for more than 150 ms** stops the motor. |

## Timeout: grate error or food blocked

On a timeout, the firmware compares the **highest current seen since the feed started** with **700**. It resets that maximum at the start of each feed, not each run. The 700 threshold is a fixed constant.

| Highest current of the feed | Meaning | Stock fault |
|---|---|---|
| < 700 | The motor turned freely but no loop was counted: the grate or its switch is faulty. | `GRAT_ERR` |
| ≥ 700 | The motor strained: food is packed. | `BLOCK_FOOD` |

This applies to every run, the reverse runs included.

## Over-current: the un-jam sequence

When the over-current rule fires, the stock firmware:

1. stops the motor and waits **500 ms** (50 ticks of the 100 Hz RTOS clock);
2. runs the motor **in the other direction** for **1 loop**, or for **2 loops** if the previous attempt was less than 3 s earlier. This run gets its own timeout;
3. gives up after **5 attempts**: the feed ends as `BLOCK_FOOD`. The attempt counter is reset at the start of each feed.

A jam while reversing flips the motor back to forward the same way, and counts as an attempt.

## After a run: the "replenish" step

When a run ends normally, in either direction, the firmware compares the loops asked for the feed with the forward loops done. If some are missing, it starts a forward run for the rest. That is how a feed continues after an un-jam reverse.

## Cancelling

A feed can be cancelled from the app, or with a short click of the FUNC button (see [hmi.md](hmi.md)). The firmware:

1. stops the motor and waits 100 ms;
2. ends the feed if the outlet is closed;
3. otherwise runs **one more loop** to close the outlet, then ends the feed.

## Button feed (hold to feed)

Holding the FUNC button for about 1.5 s starts a button feed:

- each run asks for **5 loops**;
- at the end of each run, the firmware starts another one if the button is still held;
- when the button is released, the feed is cancelled as above, so the outlet is always left closed.

The stock "manual lock" (child lock) setting blocks this.

## Start checks

Before a feed starts, the stock firmware refuses it in three cases:
- a `DC_ERR` power fault;
- a `SYSTEM_ERR`;
- batteries at or below 5.499 V while running on batteries.

See [power.md](power.md).

## ESPHome implementation

`feeder::MotorController` is ticked every 10 ms from the YAML. It is given:
- the time;
- the motor current, sampled 32 times and multiplied by 1000 × 16 / 11 while the motor runs;
- the GPIO27 level;
- the GPIO14 level.

GPIO27 and GPIO14 edges are forwarded from their `binary_sensor`s.

```
IDLE ──start(N) / start_hold()──► RUNNING ──loops counted──► replenish? / held? ──► STOPPING ──► IDLE
                                   │   ▲                                            (brake 200 ms, then sleep)
                      over-current │   │ after 500 ms
                                   ▼   │
                                 PAUSE (brake) ──► RUNNING in the other direction, 1 or 2 loops
            cancel() ──► CANCEL_WAIT (100 ms) ──► outlet closed? end : one loop, then end
```

When a feed ends, the YAML publishes:
- `Motor fault`: `None`, `GRAT_ERR` or `BLOCK_FOOD`;
- `Last feed max motor current`;
- `Last feed un-jam attempts`;
- `Dispensed food scoops`;
- `Outlet blocked`;
- `esphome.feeder_food_dispensed` or `esphome.feeder_fault` events.

The stock firmware does not beep on faults, and neither does ESPHome.

### ESPHome choices

| Point | ESPHome choice | Why |
|---|---|---|
| Brake before sleep | 200 ms | The stock firmware brakes, then sleeps once idle; the exact delay depends on its task timing. |
| A released hold feed | Reported as a normal end (`None`) | The stock firmware goes through its cancel path; for Home Assistant, a release is the expected way to end it. |

## Changes from the community config

- **Reverse never worked there.** The reverse switch did not wake the driver (GPIO19).
- **Stopping now brakes.** The community config cut all three pins, which let the motor coast.
- **Loops are counted like stock.** The community config counted every GPIO27 press, with no re-latch, so switch bounce could count twice.
- **Food low comes from the feed results**, as stock does on these units: a feed with no food seen at the outlet sets it, a feed with food clears it. The community config guessed it from a ratio of GPIO14 counts.
- **The button follows stock gestures.** A click cancels, and holding feeds while held. The community config fed a set amount on each press; that is still available from Home Assistant (`Dispense food`).
