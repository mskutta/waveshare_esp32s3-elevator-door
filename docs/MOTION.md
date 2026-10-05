# Motion settings and fault behavior

Defaults are taken from the ESP8266 firmware, with opening acceleration restored to **400,000**. The old `DOOR_DWELL_INFINITE` constant was actually **600,000 ms (10 minutes)**; the front door still closes after that interval. Updating firmware defaults does not overwrite saved motion tuning; change Opening acceleration in the web page while closed and released to update an existing controller.

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
- Opening completion tolerance **128 microsteps**: encoder threshold **18,472** at default travel.
- Opening retry limit **2**, release pause **1,000 ms**, speed/acceleration divisor **3**.

Tic speed units are **steps per 10,000 seconds**: 90,000,000 means 9,000 microsteps/s. Acceleration/deceleration units are **steps/s² × 100**: 300,000 means 3,000 microsteps/s². The step mode is fixed at 1/8.

## Validation and application

Travel accepts 160–1,000,000 microsteps. Speeds accept 10,000–500,000,000; acceleration/deceleration accept 100–2,147,483,647. Current accepts 100–3,093 mA (the T500 protocol table limit, **not** assurance your motor/driver can thermally sustain it). Dwell accepts 100–3,600,000 ms; settle 0–5,000 ms; movement deadlines 100–600,000 ms. Drift thresholds must be positive and below travel. Opening/reopening/closing deadlines must exceed the ideal acceleration/cruise/deceleration time for the configured travel.

Retry limit accepts 0–3 additional attempts, pause 100–5,000 ms, and divisor 1–10. Completion tolerance must be positive and at most 5% of travel. Enabled retries must retain valid Tic speed and acceleration after division. The deadline need not accommodate all possible retries: pauses and remaining motion consume the same deadline, and timeout takes priority.

The fixed encoder and microstep conversion is not editable. Tune only while physically closed, homed, with a healthy Tic and released motor. The motor task reserves that state before saving; it ignores open requests during the reservation, rechecks state before committing motion changes, and applies new values to the next movement. Settings use versioned JSON in NVS namespaces `door-front` and `door-rear`; invalid/corrupt/unsupported saved data falls back to that build's defaults. Saved versions 1 and 2 migrate in memory to version 4, retaining validated motion/QLab values and adding retry defaults. Version-1 network settings are discarded; Ethernet always uses DHCP. For legacy travel below 2,560 microsteps, tolerance is reduced to 5% of travel; the divisor is reduced where needed to keep existing low speed/acceleration valid. Saved versions 1–3 additionally gain a disabled `beam_break` mapping while retaining existing settings. The next save writes schema version 4. Other unknown schema versions are not migrated.

## Opening qualification and retries

Opening succeeds only when the closed switch is inactive and the encoder reaches `travel − openCompletionTolerance`. Reaching the Tic target alone does not prove actual travel. Success releases the motor and starts the existing normal or reopening dwell; it qualifies this operating cycle for a later closed cue.

Motor position ahead of the encoder by more than `openDrift`, or a Tic target reached without encoder-confirmed completion, starts a retry pause. Release the motor, wait the configured pause, then read the current encoder position and synchronize the Tic to that measurement before commanding the same absolute open target. The encoder remains continuous, including backward movement during the pause. Retries divide the applicable opening/reopening speed and opening acceleration by the configured divisor; opening current and deceleration remain unchanged.

The retry budget is shared by all opening/reopening episodes until the cycle closes. Repeated open commands cannot replenish it. Retry pauses stay internally within `opening`/`reopening`, so they do not emit repeated ordinary opening event cues. Web and USB status report encoder progress, completion threshold, retries used/allowed, pause/activity, and closed-cue eligibility.

The opening deadline starts with the first movement of an opening episode. Retry pauses and attempts never restart it; a later reopening episode has its own opening deadline but uses the remaining cycle retry budget. Exhaustion or timeout requests motor release and latches a fault. Faults clear qualification and pending closed delivery. Startup, manual confirmation, partial opening, and fault recovery cannot create a closed trigger.

## Obstruction and manual movement

While closing, a broken beam, negative drift beyond the closing threshold, or an open command causes reopening. Positive drift beyond the threshold reports `forced` and waits the short dwell. While opening, encoder-leading drift still reports `forced` and waits the short dwell; that partial opening does not qualify a closed cue. Motor-leading drift invokes the retry policy above. The beam does not block opening or retries. Reopening uses short dwell. A broken beam during waiting continually extends the short dwell and takes priority over a deadline that just expired.

When normal closing reaches its target without reaching the switch, the controller performs slow final homing. A beam break or expired homing deadline aborts it and faults. Network activity never supplies motion timing or sensor decisions.

## Fault recovery and Tic hardware watchdog

Opening retry exhaustion, opening/closing/homing timeout, Tic or MCP23008 communication failure, and encoder initialization/range failure latch faults. The controller requests release, requests lighting outputs off (an unreachable expander may retain its latch), cancels the pending closed trigger, and stops resetting the Tic command watchdog. The web reset action only clears a fault when Tic and MCP23008 communication are restored and the motor is released; manually confirm closed position again. Initial/recovery position confirmation does not advance QLab.

**Verify the Tic's own command-timeout and safe-start settings using Pololu's utility.** Keep a command timeout enabled (typically 1,000 ms) and verify its configured stopping behavior. Firmware normally resets it every 100 ms. If the I²C bus fails, the ESP32 cannot guarantee delivery of a release command; the Tic's independent timeout is therefore an essential commissioning check. If reset is requested while a permanent encoder fault persists, the fault remains latched.
