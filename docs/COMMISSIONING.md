# Commissioning and acceptance

Software verification completed: `frontdoor`, `reardoor`, and motor-inhibited `bench` builds; host tests with sanitizers and a localhost QLab mock. These do not certify the wiring, actual NVS, encoder counter under load, physical motion, or the installed QLab workspace. The retry changes have not been uploaded or physically exercised by this software verification.

## 1. Electrical checks and USB

1. With motor and lighting load power disconnected, follow [the wiring guide](WIRING.md). Confirm board orientation/header numbers and no camera connection.
2. For the internal-pull-up trial, leave external encoder resistors out. Before connection, verify A/B do not actively drive 5 V. After uploading and booting firmware, verify A/B rise to approximately 3.3 V despite the 5 V encoder supply. Verify the shifter LV bus rises to 3.3 V and HV bus to 5 V; MCP23008 and Tic connect to HV. Verify the OLED voltage rating and bus placement before attachment. Confirm shared signal grounds and output-driver off-state bias during reset.
3. Upload `bench` over USB. Confirm 115200-baud output, the OLED, and an IP from DHCP. Verify the Tic address 14, MCP23008 0x20, and OLED 0x3C; web/USB status must report the expander healthy. Set and test the Tic's own command timeout/safe-start behavior in Pololu's utility.
4. Rotate the encoder by hand: one revolution should give 600 counts / 1,600 microsteps, positive toward opening. Close the switch to confirm/reset position; verify no jumps or lost pulses. Exercise both directions repeatedly past ±30,000 counts to check PCNT rollover accumulation.
5. Confirm switch/beam active-low indications on the web page. Press each front button and confirm its web/USB indication without motion or QLab events. Verify front MCP GP7/GP5 active-high and rear GP6/GP7 active-low output polarity with a meter/driver indicators; expander pins must not supply load current.
6. Use the web **Open door (test)** button (or send an OSC open command) with the Tic connected and closed position confirmed in `bench`; verify a motor-inhibited fault, no energization, and no movement.

## 2. Settings and networking

1. Save/reload settings and reboot; confirm NVS persistence and correct front/rear namespaces. Confirm first boot starts with QLab mappings disabled.
2. Check retry defaults (2 attempts, 1,000 ms pause, divisor 3, tolerance 128). Reject limit above 3, pause outside 100–5,000 ms, divisor outside 1–10, nonpositive tolerance or tolerance above 5% of travel, and derived retry speed/acceleration below Tic limits. Check invalid JSON, invalid numeric/string types, oversized strings, impossible travel/deadlines, and attempted network/static-IP overrides are rejected without altering saved settings.
3. Confirm motion saves require closed, homed, released, healthy hardware. Attempt an open during a save; it must not be deferred into a later cycle.
4. Confirm the controller acquires a DHCP address on boot, including after loading old version-1 settings with static addressing selected. Verify saved versions 1 and 2 retain motion tuning and QLab settings while adding retry defaults; versions 1–3 also gain a disabled `beam_break` mapping while retaining existing settings; the next save must report schema 4. Test small-travel/low-acceleration legacy tuning, whose tolerance/divisor may be reduced to remain valid. Confirm the web page has no address-setting controls and the API rejects network overrides. Run USB `network-reset`; verify DHCP reacquires an address and motion/cue mappings remain unchanged. Identify the acquired address via OLED, USB status, or DHCP leases.
5. Remove the Ethernet cable and restore it, including while a slow browser request is pending. Motor control must continue, diagnostics must reflect link state, and incoming stale partial frames must not execute after recovery.

## 3. Controlled motion, each production profile

1. Verify travel/mechanism and load-clear conditions; upload the correct production profile. Boot with switch active, then inactive. Confirm no automatic startup motion in either case and no startup closed cue.
2. Manually close to establish position. Open, finish opening, dwell, close, and confirm switch-based zero. Verify front 10-minute dwell and faster normal opening; rear 5-second dwell and slower normal opening. Reopening uses the fast speed and 2-second dwell.
3. Verify both the web **Open door (test)** button and OSC requests open from confirmed closed. Repeated opens during opening/waiting must not extend dwell; an open during closing reopens. Confirm the web button cannot bypass unknown position, faults, or a settings-save reservation. Test opening follows the normal cycle and can start configured QLab event cues.
4. Break the beam during opening, waiting, and closing. Confirm opening and retries continue with the beam broken, waiting prevents closing, and closing reopens. A broken beam at a just-expired dwell must prevent closing.
5. Check closing homing at low load. Beam obstruction during final homing must stop/release and fault. Test opening, closing, and homing deadlines using controlled conditions; motor release, fault display, and operator reset are required.
6. Test encoder drift thresholds and forced events with controlled resistance/manual movement. Verify no encoder-count discontinuity while the web page polls and Ethernet is busy. With the actual cable length and motor running, check counts through repeated cycles at maximum configured speed. If the internal pull-ups give missed/noisy pulses, fit external 4.7 kΩ A/B pull-ups to 3.3 V and repeat these checks.
7. Before inducing retries, verify encoder accuracy against actual movement under motor load. Missed encoder pulses can resemble motor slip; compare both measured travel and reported counts. Under controlled load, test an early stall, partial progress followed by recovery, and a Tic target reached with insufficient encoder travel. Verify one-second motor release, latest-position Tic synchronization without encoder zeroing, and reduced retry speed/acceleration with unchanged current/deceleration. Verify backward movement during the pause is reflected in the next attempt.
8. Confirm two retries after the first attempt, repeated commands cannot refill the budget, and reopening uses the remaining budget. Verify retry pauses do not repeat ordinary opening cues. Test exhaustion and a deadline expiring during a pause: release and latched fault, followed by operator reset and manual closed confirmation. All retry pauses must consume the original opening deadline. Host tests cover timer rollover; confirm elapsed timing on hardware.
9. Verify closed-cue eligibility remains false below 18,472 microsteps with default travel, or while the closed switch remains active. Confirm a recovered opening can qualify, while partial forced opening, failed retries, timeout, and fault recovery cannot create a pending closed trigger. Repeat with the beam broken.
10. Interrupt Tic communications under controlled conditions. Confirm fault and the Tic's independent timeout response even when I²C release cannot be delivered. Restore communications, reset the fault, and manually reconfirm closed. No recovery closed cue should fire.

## 4. Actual QLab 5

1. Configure both TCP/SLIP output patches. Verify only the matching controller's address opens each door, and UDP/length-prefix/argument-bearing commands cannot trigger it.
2. Configure the chosen workspace, control permissions/passcode, and event cue mappings. Verify authentication, `/alwaysReply`, cue targeting, and diagnostics against the actual QLab version.
3. Set up and test the QLab-side idempotency guard on the sequence-advancing closed cue. Test an encoder-qualified closed cycle with QLab disconnected, then reconnect within expiry: exactly one slot should be pending; duplicate starts must not advance the sequence again.
4. Drop a reply/connection after sending closed. Verify retries, successful acknowledgment clearing, expiry, and no extra send after acknowledgment.
5. Reopen before delivery; verify cancellation. Repeat with expiry, controller reboot, and loss of switch confirmation. Reconnection by itself must not trigger closed.
6. Run partial/failed opening and fault-recovery cases with QLab connected; the sequence-advancing closed cue must not start. A successful retry followed by closure must retain acknowledgment, expiry, and cancellation behavior.
7. Confirm other offline events are not replayed. Restart QLab and unplug/replug Ethernet during motion; local control must continue.

Record measured encoder direction/rate, voltages, full-travel timing, tuned values, firmware environment, QLab version/workspace ID, and acceptance results for each physical door. Back up the final `/api/config` JSON.

With motor power inhibited, disconnect the MCP23008 to verify a latched communication fault, requested motor release, and closed-trigger cancellation. Restore the bus and verify automatic expander initialization leaves outputs off; use web fault reset and manual closed confirmation before operation. Verify actual driver behavior if the expander loses communication while an output is active; its latch may retain that level.

## Beam-break cue before initialization

With the motor released, leave the closed switch inactive and configure the master cue enable plus `beam_break` cue mapping. Confirm the event-row note is visible. A beam blocked at boot must not start a cue; clear it, then break it and verify one QLab start. Holding it blocked must not repeat; clearing and breaking again must start one more cue. Verify faults suppress starts and resetting a fault with an already-blocked beam does not trigger. Confirm closed position, then verify breaks remain suppressed even after position loss or fault reset until reboot. Disconnect QLab, break the beam, reconnect, and verify no replay. Repeat for both doors.
