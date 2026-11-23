# Feeding schedule

## Stock behaviour

**Storage.** The app sends the schedule as JSON, and the feeder stores it as-is in flash. The main keys are:

| Key | Content |
|---|---|
| `schedule[]` | Entries, each with `re` (the days it repeats on) and `it[]` (the feeds) |
| item `id` | When the feed happens (see below) |
| item `a` | Amount, in scoops |
| item `t` | Item type |
| `latest[]` | The next upcoming feeds, already worked out |
| `nextTick` | Bookkeeping for the next feed |

**When a feed happens.** The time is encoded in the item id, in seconds since midnight:

| Id form | Meaning |
|---|---|
| `n_<secs>` | Every repeat day at that time |
| `d_YYYYMMDD_<secs>` | Once, on that date |
| `s_YYYYMMDD_<secs>` | Once, with an extra delay of 0–39 s that depends on the user. This spreads server load. The delay is skipped on batteries (GPIO21 high). |

`re` lists the repeat days as digits '1' to '7', each stored as one bit.

**Running offline.**
- Once a minute, the firmware compares the clock with the upcoming feeds and starts the motor for the ones that are due.
- This does not depend on the network: only the RTC has to be valid.
- The start of a timed feed is not reported to the cloud; its end (result, amount, error code) is reported. When the feeder is offline, that report waits in a persistent queue.

**Low-power loop (battery).** Between feeds, the ESP32 goes into light sleep. How long depends on the time `c` until the next feed:

| `c` | Sleep |
|---|---|
| ≤ 30 s | none: the feed is about to start |
| 31–570 s | `c − 30` s: wakes up 30 s before the feed |
| more | 540 s: wakes every 9 minutes to check the clock |

It also wakes on the FUNC button (GPIO34), the SET button (GPIO0) and the battery line (GPIO21), active low. See [power.md](power.md).

## ESPHome behaviour

- The schedule has 24 hourly slots. Each is a Home Assistant `number` (0–6 scoops) and is saved in flash, so it also runs without Home Assistant.
- At the top of each hour, the slot for that hour is fed.
- The schedule is simpler than stock: no minutes, no repeat days, no one-off dates.
- Light sleep is not implemented: the ESP32 stays awake with Wi-Fi on, so running on batteries will drain them quickly.
