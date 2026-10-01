# Elevator doors — Waveshare ESP32-S3 Ethernet

PlatformIO/Arduino firmware for **two separate controllers**, built as `frontdoor` and `reardoor`. Target: Waveshare ESP32-S3-POE-ETH / ESP32-S3-ETH, ESP32-S3R8 + W5500, 16 MB flash and 8 MB PSRAM. Use local controller power and Ethernet for data. No camera is attached.

The Tic T500, encoder, OLED, closed switch, beam sensor, and existing lighting drivers remain. GPIO replaces the MCP23008. Wi-Fi and Bluetooth radios are disabled; MQTT, Wi-Fi provisioning, and OTA are absent. The Arduino `WiFi` library still appears in the dependency graph for radio-disable calls and the framework's shared Ethernet interface registration; no wireless connection or access point is started. OSC and HTTP use nonblocking lwIP sockets.

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

Use a data-capable USB-C cable. If automatic upload cannot enter the bootloader, hold BOOT, tap RESET, release BOOT, and retry. USB enumeration can change between bootloader and running firmware. Upload does not erase NVS settings. There is no OTA endpoint.

Exact dependencies: Espressif32 platform 6.13.0, Arduino-ESP32 2.0.17 from that platform, Tic 2.2.0, SSD1306Ascii 1.3.5, ArduinoJson 7.4.3. The supplied SpookMMWave custom board and W5500 integration are the starting point. The application/network task runs on core 0; the higher-priority motor task and PCNT ISR run on core 1.

## Configure

Connect Ethernet to a network with DHCP. Find the controller IP on its OLED, with USB `status`, or in your DHCP server's lease list. Open `http://<controller-ip>/`. Hostnames are `elev-door-front-XXXXXX` and `elev-door-rear-XXXXXX`; this firmware does **not** publish mDNS, so use an IP or a DHCP/DNS reservation.

The page provides live diagnostics, DHCP or static IP settings, QLab workspace/OSC settings, one cue per event, motion tuning, and a fault-reset action. There is no authentication, as requested; use the intended show-control network. Passcodes are saved in NVS and available through the configuration API on that network.

QLab triggers and all cue mappings are disabled initially. Configure them deliberately. Motion defaults reproduce the old project; tuning is explained in [motion settings](docs/MOTION.md). Saves reserve a released, idle motor before writing NVS. Motion changes additionally require a confirmed closed door. A healthy Tic must be connected for saves and network recovery, so the firmware can verify the motor is released. Unknown-position and fault states allow communications-only saves when the Tic is reachable and released. The reservation expires after 10 seconds if abandoned.

Static-IP changes apply after the save response; reopen the page at the new address. USB serial accepts newline-terminated commands:

- `status`: door state, encoder/motor position, sensors, pending closed trigger, fault, and IP.
- `network-reset`: persist DHCP while retaining motion settings and cue mappings. It uses the same idle/released guard as web saves.

## Door operation

Startup releases the motor. An active closed switch confirms position without starting any QLab cue. Otherwise manually close the door. Remote open commands are ignored until position is confirmed.

An open command opens from closed or reopens during closing. Repeated commands during opening or waiting do not extend dwell. Front buttons remain inactive; the front up acceptance light turns on when closed and clears on an open request. Rear EL-wire outputs are active-low, on during opening, and off when waiting open. Faults turn the outputs off.

Beam and encoder drift handling remain local. Network outages do not stop local motion or automatically open the door. The beam takes priority over an expired dwell and prevents starting a close while broken. Beam obstruction during final closing homing stops/releases and faults. Tic communications, opening/closing deadlines, and homing deadlines latch faults. Fault reset does not move the door; closed-position confirmation is required again. Never feed the Tic command watchdog while faulted.

## OSC/TCP and QLab

Incoming TCP port **53000**, OSC 1.1 double-END SLIP framing, one argument-free command per build:

- Front: `/elev-door-front/door/open`
- Rear: `/elev-door-rear/door/open`

Two simultaneous incoming clients are supported. Frames are limited to 2048 bytes. An unfinished frame times out after two seconds. Unsupported addresses, arguments, bundles, invalid padding/escaping, and oversized frames are rejected. This is TCP OSC, not UDP or plain text, and not length-prefixed OSC 1.0.

Outgoing QLab cue starts use a separate TCP connection, configured host/port (default 53000), workspace ID, and optional passcode. See [QLab setup and replay semantics](docs/QLAB.md).

Only the **closed** trigger has one pending RAM slot. It is created after an actual operating cycle, coalesces repeated offers without refreshing its age, retries until QLab acknowledges it, and expires after 30 seconds by default. Reopening, loss of closed confirmation, reboot, or a configuration commit cancels it. Other enabled mappings are live-only, use a bounded 16-event buffer, expire after one second, and are discarded on disconnection; uncertain live sends are not retried. Disabled mappings do not start cues.

## Validation

```sh
python3 scripts/test_native.py
```

Requires a C++17 compiler and the front-door dependencies installed by `pio run -e frontdoor`. Tests compile the actual portable state machine, configuration codec/storage wrapper, TCP module, and HTTP API with AddressSanitizer/UndefinedBehaviorSanitizer. A local TCP QLab mock verifies handshake, fragmentation, cue acknowledgment, retries, reconnection, and cancellation. HTTP tests verify configuration guards, rollback, and DHCP recovery. Tests briefly listen on TCP 53000 and 80; stop other services on those ports before running them. The fake Preferences backend tests persistence errors and corruption; physical ESP32 NVS still needs commissioning verification.

Both production builds and `bench` compile, and the native suite passes. **Physical upload, electrical levels, pulse-counter behavior under load, door movement, and actual QLab 5 integration have not been verified.** No ESP32 USB device was attached during implementation. Follow [wiring](docs/WIRING.md) and [commissioning](docs/COMMISSIONING.md) before powering the motor.

## Source layout and APIs

- `lib/DoorCore`: platform-independent motion state machine, settings validation, single closed slot, OSC/SLIP codec, QLab reply state machine.
- `src/DoorRuntime.cpp`: sole I²C owner; Tic, OLED, switches, output drivers, bounded mailboxes, status snapshots, and the independent motor task.
- `src/Encoder.cpp`: PCNT A-rising-edge count/B direction, 1 µs glitch filtering, rollover accumulation, and logical zero without stopping the counter.
- `src/Ethernet.cpp`, `src/OscTcp.cpp`: W5500/lwIP Ethernet and nonblocking TCP sockets. DNS lookup runs only on the network core.
- `src/Config.cpp`, `src/WebUI.cpp`: versioned JSON in per-door NVS namespaces and the embedded web page.

HTTP: `GET /api/state`, `GET /api/config`, full-document `PUT /api/config` (schema version 1; JSON Content-Type; maximum body 8192 bytes), and body-free `POST /api/fault/reset`. Headers are limited to 2048 bytes; duplicate Content-Length, chunked transfer, and oversized requests are rejected before JSON allocation. One client is served at a time, with a five-second total connection deadline and bounded receive/transmit work per loop. Malformed configuration returns 400; incompatible controller state or persistence failure returns 409. Configurations are validated before saving. Keep a downloaded `/api/config` copy as a backup.

The two supplied reference projects are unchanged.
