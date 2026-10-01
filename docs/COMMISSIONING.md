# Commissioning and acceptance

Software verification completed: `frontdoor`, `reardoor`, and motor-inhibited `bench` builds; host tests with sanitizers and a localhost QLab mock. These do not certify the wiring, actual NVS, encoder counter under load, physical motion, or the installed QLab workspace. No ESP32 USB device was connected during implementation.

## 1. Electrical checks and USB

1. With motor and lighting load power disconnected, follow [the wiring guide](WIRING.md). Confirm board orientation/header numbers and no camera connection.
2. Verify encoder A/B rise only to 3.3 V despite the 5 V encoder supply. Verify the currently unknown OLED/Tic pull-up voltage and correct it before attachment. Confirm shared signal grounds and output-driver off-state bias during reset.
3. Upload `bench` over USB. Confirm 115200-baud output, the OLED, and an IP from DHCP. Verify the Tic address 14 and OLED 0x3C. Set and test the Tic's own command timeout/safe-start behavior in Pololu's utility.
4. Rotate the encoder by hand: one revolution should give 600 counts / 1,600 microsteps, positive toward opening. Close the switch to confirm/reset position; verify no jumps or lost pulses. Exercise both directions repeatedly past ±30,000 counts to check PCNT rollover accumulation.
5. Confirm switch/beam active-low indications on the web page. Verify front inactive buttons and front/rear output polarity with a meter/driver indicators; GPIO40/41 must not supply load current.
6. Send an open command to `bench`; verify a motor-inhibited fault, no energization, and no movement.

## 2. Settings and networking

1. Save/reload settings and reboot; confirm NVS persistence and correct front/rear namespaces. Confirm first boot starts with QLab mappings disabled.
2. Check invalid JSON, invalid numeric/string types, oversized strings, impossible travel/deadlines, and invalid static subnet settings are rejected without altering saved settings.
3. Confirm motion saves require closed, homed, released, healthy hardware. Attempt an open during a save; it must not be deferred into a later cycle.
4. Configure static IP and verify the new page address. Run USB `network-reset`; verify DHCP returns and motion/cue mappings remain. Saves require a reachable/released Tic.
5. Remove the Ethernet cable and restore it, including while a slow browser request is pending. Motor control must continue, diagnostics must reflect link state, and incoming stale partial frames must not execute after recovery.

## 3. Controlled motion, each production profile

1. Verify travel/mechanism and load-clear conditions; upload the correct production profile. Boot with switch active, then inactive. Confirm no automatic startup motion in either case and no startup closed cue.
2. Manually close to establish position. Open, finish opening, dwell, close, and confirm switch-based zero. Verify front 10-minute dwell and faster normal opening; rear 5-second dwell and slower normal opening. Reopening uses the fast speed and 2-second dwell.
3. Verify repeated opens during opening/waiting do not extend dwell; an open during closing reopens.
4. Break the beam during opening, waiting, and closing. Confirm local dwell/reopening behavior. A broken beam at a just-expired dwell must prevent closing.
5. Check closing homing at low load. Beam obstruction during final homing must stop/release and fault. Test opening, closing, and homing deadlines using controlled conditions; motor release, fault display, and operator reset are required.
6. Test encoder drift thresholds and forced events with controlled resistance/manual movement. Verify no encoder-count discontinuity while the web page polls and Ethernet is busy.
7. Interrupt Tic communications under controlled conditions. Confirm fault and the Tic's independent timeout response even when I²C release cannot be delivered. Restore communications, reset the fault, and manually reconfirm closed. No recovery closed cue should fire.

## 4. Actual QLab 5

1. Configure both TCP/SLIP output patches. Verify only the matching controller's address opens each door, and UDP/length-prefix/argument-bearing commands cannot trigger it.
2. Configure the chosen workspace, control permissions/passcode, and event cue mappings. Verify authentication, `/alwaysReply`, cue targeting, and diagnostics against the actual QLab version.
3. Set up and test the QLab-side idempotency guard on the sequence-advancing closed cue. Test a closed cycle with QLab disconnected, then reconnect within expiry: exactly one slot should be pending; duplicate starts must not advance the sequence again.
4. Drop a reply/connection after sending closed. Verify retries, successful acknowledgment clearing, expiry, and no extra send after acknowledgment.
5. Reopen before delivery; verify cancellation. Repeat with expiry, controller reboot, and loss of switch confirmation. Reconnection by itself must not trigger closed.
6. Confirm other offline events are not replayed. Restart QLab and unplug/replug Ethernet during motion; local control must continue.

Record measured encoder direction/rate, voltages, full-travel timing, tuned values, firmware environment, QLab version/workspace ID, and acceptance results for each physical door. Back up the final `/api/config` JSON.
