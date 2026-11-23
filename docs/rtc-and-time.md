# RTC and timekeeping

## Stock behaviour

**Chip.** The RTC is a PCF8563 / BM8563 at I2C address `0x51`. The firmware reads registers `0x02`–`0x08` (seconds to year, in BCD).

**It stores local time**, not UTC. The time zone and DST come from the app. The default zone is UTC+8, and the DST flag removes one hour.

**Validity check.** A year outside 2010–2050 counts as invalid. In that case, the firmware resets the RTC to 2019-01-01 00:00 (UTC+8) and sets an `RTC_COM_ERR` flag. It never looks at the chip's "voltage low" (VL) bit.

**Calibration.**
- The software clock runs on the ESP32 timer.
- Every hour, and at midnight, the firmware re-aligns it on the RTC.
- If the RTC read fails, it retries after 60 s. After 11 failures in a row, it raises an error event.
- When the app sends a new time, a jump of more than about 208 days, or any jump backwards, is refused.

**Countdown timer.** The RTC's own countdown timer (registers `0x0E` / `0x0F`) is set to 60 minutes and acts as a hardware watchdog. It does not wake the ESP32.

**Time sources.** There is no SNTP in the stock firmware: the time only comes from the PETKIT cloud or the app. Over BLE, the feeder sends its own time to relay devices; it does not receive time that way.

**When the time is untrusted.** While `RTC_COM_ERR` is set and no valid time has come from the app, the firmware does not trust its list of upcoming schedule entries.

## ESPHome behaviour

- At boot, `pcf8563.read_time` loads the RTC into the system clock. Schedules then run offline.
- Two sources set the clock, and each sync writes the time back to the RTC (`on_time_sync` → `pcf8563.write_time`), so the next offline boot starts with a fresh time. The community config never wrote the RTC.
  - **Home Assistant time** over the API. This works even on networks that block NTP, which was the case on the first feeder flashed: its clock still read 1970 under the community config.
  - **SNTP**: the server from `secrets.yaml`, then `0.pool.ntp.org` and `1.pool.ntp.org` as fallbacks.
- `wifi.reboot_timeout: 0s` keeps the feeder running when Wi-Fi is down. Without it, ESPHome would reboot every 15 minutes, possibly in the middle of a feed.

**First boot after flashing.** ESPHome's PCF8563 driver stores **UTC**, while the stock firmware stored **local time**. Until the first SNTP sync, the clock is off by the stock offset (one or two hours in Paris).
