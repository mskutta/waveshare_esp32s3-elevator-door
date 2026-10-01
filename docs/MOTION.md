# Motion settings and fault behavior

Defaults are taken from the ESP8266 firmware. The old `DOOR_DWELL_INFINITE` constant was actually **600,000 ms (10 minutes)**; the front door still closes after that interval.

## Default values

- Travel: **18,600 microsteps**, closed at zero.
- Opening speed: front **90,000,000**, rear **20,000,000**; reopening **90,000,000** on both.
- Closing speed: **30,000,000**. Homing velocity: **−10,000,000** (web input is its positive magnitude).
- Opening acceleration/deceleration: **400,000 / 700,000**.
- Closing acceleration/deceleration: **100,000 / 100,000**.
- Homing acceleration/deceleration: **100,000 / 100,000**.
- Requested current: opening/homing **1,500 mA**; closing **900 mA**. The T500 chooses its nearest supported current at or below the request; these requests become 1,452 mA and 880 mA with the pinned library.
- Normal dwell: front **600,000 ms**, rear **5,000 ms**. Short dwell **2,000 ms**; settle **250 ms**.
- Opening/closing deadline: **30,000 ms** each. Homing deadline **5,000 ms**.
- Closing drift threshold **64 microsteps**; opening drift threshold **128 microsteps**.

Tic speed units are **steps per 10,000 seconds**: 90,000,000 means 9,000 microsteps/s. Acceleration/deceleration units are **steps/s² × 100**: 400,000 means 4,000 microsteps/s². The step mode is fixed at 1/8.

## Validation and application

Travel accepts 160–1,000,000 microsteps. Speeds accept 10,000–500,000,000; acceleration/deceleration accept 100–2,147,483,647. Current accepts 100–3,093 mA (the T500 protocol table limit, **not** assurance your motor/driver can thermally sustain it). Dwell accepts 100–3,600,000 ms; settle 0–5,000 ms; movement deadlines 100–600,000 ms. Drift thresholds must be positive and below travel. Opening/reopening/closing deadlines must exceed the ideal acceleration/cruise/deceleration time for the configured travel.

The fixed encoder and microstep conversion is not editable. Tune only while physically closed, homed, with a healthy Tic and released motor. The motor task reserves that state before saving; it ignores open requests during the reservation, rechecks state before committing motion changes, and applies new values to the next movement. Settings use versioned JSON in NVS namespaces `door-front` and `door-rear`; invalid/corrupt/unsupported saved data falls back to that build's defaults. No automatic settings migration is performed for unknown schema versions.

## Obstruction and manual movement

While closing, a broken beam, negative drift beyond the closing threshold, or an open command causes reopening. Positive drift beyond the threshold reports `forced` and waits the short dwell. While opening, excessive absolute drift reports `forced` and waits; a broken beam reduces normal dwell to the short dwell. Reopening uses short dwell. A broken beam during waiting continually extends the short dwell and takes priority over a deadline that just expired.

When normal closing reaches its target without reaching the switch, the controller performs slow final homing. A beam break or expired homing deadline aborts it and faults. Network activity never supplies motion timing or sensor decisions.

## Fault recovery and Tic hardware watchdog

Opening/closing/homing timeout, Tic communication failure, and encoder initialization/range failure latch faults. The controller requests release, turns off lighting outputs, cancels the pending closed trigger, and stops resetting the Tic command watchdog. The web reset action only clears a fault when Tic communication is restored and the motor is released; manually confirm closed position again. Initial/recovery position confirmation does not advance QLab.

**Verify the Tic's own command-timeout and safe-start settings using Pololu's utility.** Keep a command timeout enabled (typically 1,000 ms) and verify its configured stopping behavior. Firmware normally resets it every 100 ms. If the I²C bus fails, the ESP32 cannot guarantee delivery of a release command; the Tic's independent timeout is therefore an essential commissioning check. If reset is requested while a permanent encoder fault persists, the fault remains latched.
