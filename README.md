# Elevator doors — Waveshare ESP32-S3 Ethernet

PlatformIO/Arduino firmware for **two separate controllers**, built as `frontdoor` and `reardoor`. Target: Waveshare ESP32-S3-POE-ETH / ESP32-S3-ETH, ESP32-S3R8 + W5500, 16 MB flash and 8 MB PSRAM. Use local controller power and Ethernet for data. No camera is attached.

The Tic T500, encoder, OLED, closed switch, beam sensor, and existing lighting drivers remain. The 5 V MCP23008 is retained for front button inputs and lighting drivers, on the existing level-shifted I²C bus shared with the Tic. Limit and beam contacts use ESP32 GPIO. Wi-Fi and Bluetooth radios are disabled; MQTT and Wi-Fi provisioning are absent. Firmware updates use Ethernet HTTP. The Arduino `WiFi` library still appears in the dependency graph for radio-disable calls and the framework's shared Ethernet interface registration; no wireless connection or access point is started. OSC and HTTP use nonblocking lwIP sockets.

## Build and USB upload

Install PlatformIO Core or the PlatformIO VS Code extension. From this folder:

```sh
pio run -e frontdoor -e reardoor
pio run -e bench
pio run -e bench -t upload --upload-port /dev/cu.usbmodemYOURPORT
pio device monitor --port /dev/cu.usbmodemYOURPORT --baud 115200
```

`bench` uses the front-door profile and refuses all energizing/movement actions; an open request produces a visible `Bench: motor inhibited` fault. It shares the front-door settings namespace. Keep motor power disconnected during initial wiring tests as well.

After commissioning, upload the appropriate production environment:

```sh
pio run -e frontdoor -t upload --upload-port /dev/cu.usbmodemYOURPORT
# Use -e reardoor for the other controller.
```

Use a data-capable USB-C cable. If automatic upload cannot enter the bootloader, hold BOOT, tap RESET, release BOOT, and retry. USB enumeration can change between bootloader and running firmware. Upload does not erase NVS settings. Install this OTA-capable version and its new partition table once over USB; later updates use Ethernet as described below.

Exact dependencies: Espressif32 platform 6.13.0, Arduino-ESP32 2.0.17 from that platform, Tic 2.2.0, SSD1306Ascii 1.3.5, ArduinoJson 7.4.3. The supplied SpookMMWave custom board and W5500 integration are the starting point. The application/network task runs on core 0; the higher-priority motor task and PCNT ISR run on core 1.

## Firmware updates over Ethernet

After the one-time USB installation of this version, open the controller's IP address in a browser and use **Firmware update → Upload firmware over Ethernet**. Build the correct environment and select its application image:

```sh
pio run -e frontdoor
# Select .pio/build/frontdoor/firmware.bin for the front controller.
# Build/select reardoor for the rear controller, or bench for motor-inhibited testing.
```

Use this project's matching door image. Do not upload `bootloader.bin`, `partitions.bin`, a merged flash image, or the linear actuator firmware. The updater checks ESP image validity; it does not identify the project's door profile for you.

Upload immediately stops/releases the motor and clears pending cues. Flash writing begins only after the Tic confirms release. A separate update hold blocks open commands, fault resets, automatic opening, and configuration saves throughout the upload; it does not expire like a settings reservation. Successful installation reboots after the response. Normal startup behavior applies: **the front door automatically opens after homing**, including if already closed at reboot. QLab opening events follow their normal readiness rules. NVS tuning and cue mappings are retained.

A failed, disconnected, or timed-out upload does not reboot or activate the incomplete image. The running firmware remains available, with movement faulted until a web fault reset and closed-position confirmation, or a fresh manual closure after the upload hold ends. You may retry the upload first. The front door automatically opens after recovery and homing. Uploads allow 120 seconds total and stream into the inactive app slot without buffering the image in RAM. Only one HTTP client is served at a time, so status polling pauses during an upload.

The same endpoint supports a command-line upload:

```sh
curl --fail --show-error -H 'Expect:' -H 'Content-Type: application/octet-stream' \
  --upload-file .pio/build/frontdoor/firmware.bin http://10.81.95.25/api/update
```

`PUT /api/update` requires a nonzero Content-Length and `application/octet-stream`, with a 7 MiB maximum. Chunked uploads are rejected. Upload endpoints have the same unauthenticated access as the existing web controls; use the intended show-control network. OTA cannot change the partition table. The supplied 16 MB layout uses two 7 MiB application slots and preserves the existing NVS address/size.

## Configure

Connect Ethernet to a network with DHCP. Find the controller IP on its OLED, with USB `status`, or in your DHCP server's lease list. Open `http://<controller-ip>/`. Hostnames are `elev-door-front-XXXXXX` and `elev-door-rear-XXXXXX`; this firmware does **not** publish mDNS, so use an IP or a DHCP/DNS reservation.

The page provides live diagnostics, QLab workspace/OSC settings, one cue per event, motion tuning, an **Open door (test)** button, and a fault-reset action. There is no authentication, as requested; use the intended show-control network. Passcodes are saved in NVS and available through the configuration API on that network.

QLab triggers and all cue mappings are disabled initially. Configure them deliberately. Motion defaults follow the old project with opening acceleration restored to 400,000; tuning is explained in [motion settings](docs/MOTION.md). Saves reserve a released, idle motor before writing NVS. Motion changes additionally require a confirmed closed door. A healthy Tic must be connected for saves, so the firmware can verify the motor is released. Unknown-position and fault states allow communications-only saves when the Tic is reachable and released. The reservation expires after 10 seconds if abandoned.

Ethernet always obtains its address through DHCP; there are no controller-side IP settings. Saved versions 1 and 2 retain motion tuning and QLab settings while gaining opening-retry defaults; version-1 network settings are discarded. Saved versions 1–3 also gain a disabled `beam_break` mapping while retaining existing settings. New saves use schema version 4. Very short travel or low-speed/acceleration legacy profiles receive a smaller completion tolerance or retry divisor to remain valid. USB serial accepts newline-terminated commands:

- `status`: door state, encoder/motor position, sensors, opening progress/threshold, retry count and activity, closed-cue eligibility, pending closed trigger, fault, and IP.
- `network-reset`: restart DHCP to request a fresh lease without changing saved settings or moving the door. The web/QLab connections may disconnect while the address is reacquired.

## Door operation

Startup releases the motor. An active closed switch confirms position; otherwise manually close the door to establish the encoder reference. Remote open commands are ignored until position is confirmed. The front door automatically starts one normal opening cycle after its first position confirmation at boot and again after a fault reset followed by position confirmation. This sends the normal enabled `open`/`opening` QLab events when QLab is ready; offline opening events are discarded as usual. Rear-door position confirmation does not automatically open. Automatic opening waits for any settings reservation to end.

Once homed, both doors retain their position reference while powered, including when manually moved off the closed switch. An open request then starts from the current encoder position and targets the normal full-open position. Manual movement alone does not start a cycle or its closing timer. Each valid return to the closed switch re-zeros the encoder without producing a closed cue for manual movement. Faults and reboot invalidate homing. The `closed` state also represents homed idle after manual movement: check `limit` for physical closure and `homed` for a valid position reference.

The web **Open door (test)** button sends the same open request as QLab. It uses the normal door cycle, lighting, and configured event cues; it does not bypass position confirmation, fault handling, or maintenance reservations. The `bench` build still inhibits movement.

An open command opens from closed or reopens during closing. Repeated commands during opening or waiting do not extend dwell. Front button presses are read through the MCP23008 and reported in diagnostics, while remaining inactive for motion and QLab; the front up acceptance light turns on when closed and clears on an open request. Rear EL-wire outputs are active-low, on during opening, and off when waiting open. Faults request outputs off. An unreachable expander may retain its previous output latch, so driver off-state bias and hardware reset behavior must be verified. MCP23008 communication failure latches a fault and requests motor release; reconnection alone does not clear it. Once hardware is healthy, recover by a fresh manual closure or the web reset action.

Encoder A/B on GPIO48/47 use internal 3.3 V pull-ups for the initial hardware trial; retain the encoder’s 5 V supply and common ground. Validate counting under motor load at maximum configured speed, adding external 4.7 kΩ pull-ups to 3.3 V if needed. See [wiring](docs/WIRING.md) and [commissioning](docs/COMMISSIONING.md).

An opening succeeds only after the closed switch releases and encoder position reaches travel minus completion tolerance (18,472 microsteps with defaults). A stalled or short opening gets up to ten retries by default, each after a one-second motor-release pause, using one-third opening speed and acceleration. The Tic position is synchronized to the latest encoder measurement; the encoder is not reset. Opening and reopening share the cycle’s retry budget. Each opening episode, including its retry pauses, must finish within the opening deadline. Exhaustion faults and clears closed-cue eligibility. See [motion settings](docs/MOTION.md).

The configurable `beam_break` QLab event fires once per fresh obstruction only in `unknown` before the first closed-position confirmation after boot. It is suppressed during faults and permanently after initialization until reboot. An already-blocked beam must clear first; offline breaks are discarded.

Beam and encoder drift handling remain local. Network outages do not stop local motion or automatically open the door. The beam takes priority over an expired dwell and prevents starting a close while broken. Beam obstruction during final closing homing stops/releases and faults. Tic communications, opening/closing deadlines, and homing deadlines latch faults. Recover either with the web fault-reset action followed by closed-position confirmation, or by manually closing the door after hardware is healthy and the motor is released. Manual recovery requires the closed switch to be released after the fault and then pressed continuously for 100 ms. A switch already held when the fault occurs cannot reset it. Hardware failures, energized motors, firmware uploads, and settings reservations cancel pending manual recovery; after those clear, release and close again. Encoder initialization/range checks and Tic/MCP23008 communication checks must pass. Bench motor-inhibition faults require the web reset action and continue to inhibit energization. Neither manual recovery nor web reset creates a completed-cycle closed cue. For the front door, position confirmation schedules one normal automatic opening with the configured opening events; the rear remains idle and homed. Web reset can still confirm position when the switch is already active. Never feed the Tic command watchdog while faulted.

## OSC/TCP and QLab

Incoming TCP port **53000**, OSC 1.1 double-END SLIP framing, one argument-free command per build:

- Front: `/elev-door-front/door/open`
- Rear: `/elev-door-rear/door/open`

Two simultaneous incoming clients are supported. Frames are limited to 2048 bytes. An unfinished frame times out after two seconds. Unsupported addresses, arguments, bundles, invalid padding/escaping, and oversized frames are rejected. This is TCP OSC, not UDP or plain text, and not length-prefixed OSC 1.0.

Outgoing QLab cue starts use a separate TCP connection, configured host/port (default 53000), optional workspace ID, and optional passcode. A blank workspace ID targets all open workspaces listening on that port. See [QLab setup and replay semantics](docs/QLAB.md).

Only the **closed** trigger has one pending RAM slot. It is created only after an encoder-confirmed successful opening returns to the closed switch, coalesces repeated offers without refreshing its age, retries until QLab acknowledges it, and expires after 30 seconds by default. Faults, reopening, loss of closed confirmation, reboot, or a configuration commit cancel it. Partial/failed openings, startup, and fault recovery cannot advance the closed cue. Other enabled mappings are live-only, use a bounded 16-event buffer, expire after one second, and are discarded on disconnection; uncertain live sends are not retried. Disabled mappings do not start cues.

## Validation

```sh
python3 scripts/test_native.py
```

Requires a C++17 compiler and the front-door dependencies installed by `pio run -e frontdoor`. Tests compile the actual portable state machine, configuration codec/storage wrapper, TCP module, and HTTP API with AddressSanitizer/UndefinedBehaviorSanitizer. A local TCP QLab mock verifies handshake, fragmentation, cue acknowledgment, retries, reconnection, and cancellation. HTTP tests verify configuration guards, rollback, rejection of static-IP overrides, streaming firmware larger than the JSON buffer, failed release/flash/validation, disconnect/link loss, timeout cleanup, and delayed reboot. Portable state-machine tests verify the update hold prevents reset/motion beyond the normal reservation timeout. Motion tests cover opening qualification, bounded retries, shared reopening budget, beam behavior, faults, and deadline rollover. Configuration tests verify retry-setting validation and migration of versions 1/2/3 for both doors. Host OSC tests use an automatically assigned TCP port so they can run alongside QLab. HTTP tests briefly listen on TCP 80; stop other services on that port before running them. The fake Preferences backend tests persistence errors and corruption; physical ESP32 NVS still needs commissioning verification.

Both production builds and `bench` compile, and the native suite passes. **Physical upload, electrical levels, pulse-counter behavior under load, door movement, and actual QLab 5 integration have not been verified.** No ESP32 USB device was attached during implementation. Follow [wiring](docs/WIRING.md) and [commissioning](docs/COMMISSIONING.md) before powering the motor.

## Source layout and APIs

- `lib/DoorCore`: platform-independent motion state machine, settings validation, single closed slot, OSC/SLIP codec, QLab reply state machine.
- `src/Mcp23008.cpp`: checked register driver with original front/rear mappings, safe startup, button polling, and bounded recovery.
- `src/DoorRuntime.cpp`: sole I²C owner; Tic, OLED, MCP23008, switches, output drivers, bounded mailboxes, status snapshots, and the independent motor task.
- `src/Encoder.cpp`: PCNT A-rising-edge count/B direction, 1 µs glitch filtering, rollover accumulation, and logical zero without stopping the counter.
- `src/Ethernet.cpp`, `src/OscTcp.cpp`: W5500/lwIP Ethernet and nonblocking TCP sockets. DNS lookup runs only on the network core.
- `src/Config.cpp`, `src/WebUI.cpp`: versioned JSON in per-door NVS namespaces and the embedded web page.

HTTP: `PUT /api/update` (streamed binary firmware, described above), `GET /api/state`, `GET /api/config`, full-document `PUT /api/config` (schema version 4; JSON Content-Type; maximum body 8192 bytes), and body-free `POST /api/door/open` and `POST /api/fault/reset`. The open endpoint returns 202 when the request is submitted, not proof of movement; the door-control task applies normal command/state checks. Headers are limited to 2048 bytes; duplicate Content-Length, chunked transfer, and oversized requests are rejected before JSON allocation. One client is served at a time, with a five-second request deadline (120 seconds for firmware uploads), a separate five-second response deadline, and bounded receive/transmit work per loop. Malformed configuration returns 400; incompatible controller state or persistence failure returns 409. Configurations are validated before saving. Keep a downloaded `/api/config` copy as a backup.

The two supplied reference projects are unchanged.
