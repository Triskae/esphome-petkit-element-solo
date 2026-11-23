# Buttons, LED and sounds

## Buttons

There are two buttons, both active low and both light-sleep wake sources:

- **FUNC** on GPIO34;
- **SET** on GPIO0.

The stock firmware reads them with a small button state machine ticked every **10 ms**:

| Timing | Value |
|---|---|
| Debounce | 2 ticks = 20 ms |
| Click / double-click window | 20 ticks = 200 ms |
| Long press starts | 100 ticks = 1 s |
| "Hold" event while held | every 30 ticks = 300 ms |

FUNC counts the hold events after the long press starts:
- the **2nd** (about 1.5 s of holding) is the **mid-long** press;
- the **5th** (about 2.4 s) is the "long long" press.

### FUNC (feed) button

| Gesture | Stock action | ESPHome |
|---|---|---|
| Press | Wakes the screen logic, key beep (200 ms) if sounds are on | Key beep, unless `Mute sounds` is on |
| Click | Cancels the running feed | Same |
| Mid-long (≈ 1.5 s) | Starts a button feed that runs while held, unless the **manual lock** is on | Same, `Child lock` switch |
| Release after mid-long | Ends the button feed (outlet left closed) | Same |
| Double click | Same as click in normal mode | Same as click |

### SET button

| Gesture | Stock action | ESPHome |
|---|---|---|
| Press | Key beep | Key beep |
| Long | Network pairing / unbind | Not implemented (ESPHome uses its own Wi-Fi setup) |
| SET long + FUNC double, in the first second after boot | Factory test mode | Not implemented |
| "Ship mode" combination | On batteries only: power down for transport | Not implemented |

## LED (GPIO5)

The stock LED driver has five styles:

| Style | Pattern |
|---|---|
| 0 | off |
| 1 | on |
| 2 | blink, 80 ms on / 80 ms off |
| 3 | blink, 500 ms on / 500 ms off |
| 4 | blink, 200 ms on / 200 ms off |

The firmware picks the style from the network state:

| State | Style |
|---|---|
| Pairing / connecting | 2 (fast blink) |
| Connected to the cloud | 1 (on) |
| Offline | 3 (slow blink) |

When the user turns the LED "off" in the app, the blinking styles become a short flash: 50 ms on, 1.5 s off.

ESPHome maps it as follows:
- on while Home Assistant is connected;
- slow blink otherwise.

## Sounds (GPIO16)

The buzzer runs at 2 kHz. The stock beep types are equal on/off pulses:

| Type | Pulse |
|---|---|
| 1 | 25 ms |
| 2 | 50 ms |
| 3 | 200 ms |
| 4 | 800 ms |

| Event | Stock sound | ESPHome |
|---|---|---|
| Button press | Type 3, once | 200 ms beep |
| Feed start (not from the button) | Type 3, once, if the "feed sound" setting is on | 200 ms beep, unless muted |
| Food low | 5 × (500 ms on, 500 ms off), repeated every 30 s, see [power.md](power.md#food-level) | Same, `Food low alarm` switch and hours |
| Feed end, faults | none | none |
