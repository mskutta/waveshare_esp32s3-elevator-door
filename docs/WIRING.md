# Wiring migration

Header positions below are verified against [Waveshare's official pin diagram](https://docs.waveshare.com/assets/images/ESP32-S3-ETH-details-15-b9d2e97a5122db37da6be5bac19e29b4.webp), linked from [the board documentation](https://docs.waveshare.com/ESP32-S3-ETH). View the **component side**, USB-C at the top and microSD at the bottom: physical pin 1 is top right, pin 20 bottom right, pin 21 bottom left, and pin 40 top left. GPIO numbers are different from physical header positions.

## Both doors

- **I²C SDA → GPIO17, physical pin 34.** Previously NodeMCU D2 / GPIO4, as actually passed to `Wire.begin(D2,D1)` in the old code. Its SDA/SCL comment was reversed. Connect the Tic SDA and OLED SDA here.
- **I²C SCL → GPIO18, physical pin 31.** Previously NodeMCU D1 / GPIO5. Connect Tic SCL and OLED SCL here.
- **Encoder A → GPIO15, physical pin 29.** Previously NodeMCU D5 / GPIO14.
- **Encoder B → GPIO16, physical pin 32.** Previously NodeMCU D6 / GPIO12. A rising with B low counts positive; B high counts negative.
- **Closed limit contact → GPIO21, physical pin 35.** Previously MCP23008 pin 0. Other contact goes to signal ground. Closed/active pulls low.
- **Beam sensor contact → GPIO1, physical pin 25.** Previously MCP23008 pin 1. Other contact goes to signal ground. Broken/active pulls low.
- **Signal ground → physical pin 28 or 33.** Other header grounds are 3, 8, 13, 18, 23, and 38. Connect controller, encoder, Tic signal ground, OLED, sensor contacts, and driver input grounds appropriately.
- **3.3 V → physical pin 36.** Use for the encoder A/B pull-ups and compatible I²C pull-ups. Do not use 3V3_EN (pin 37) as a power rail.

Tic address remains **0x0E** (device number 14); OLED is **0x3C**, SSD1306 128×64. Match the Tic's device-number and I²C settings with the Pololu configuration utility. Remove the MCP23008 from the bus and move its signal wires to the GPIO connections above/below.

## Front door

- **Up button contact → GPIO38, physical pin 14.** Previously MCP23008 pin 6. Other side to ground. Configured with a pull-up, but intentionally inactive in firmware.
- **Down button contact → GPIO39, physical pin 12.** Previously MCP23008 pin 4. Other side to ground. Also intentionally inactive.
- **Up acceptance driver input → GPIO40, physical pin 11.** Previously MCP23008 pin 7. HIGH is active; LOW is off.
- **Down acceptance driver input → GPIO41, physical pin 10.** Previously MCP23008 pin 5. HIGH is active; LOW is off. Preserved behavior leaves this light off.

## Rear door

- **First EL-wire driver input → GPIO40, physical pin 11.** Previously MCP23008 pin 6. LOW is active; HIGH is off.
- **Second EL-wire driver input → GPIO41, physical pin 10.** Previously MCP23008 pin 7. LOW is active; HIGH is off.

GPIO40/41 connect to the **existing confirmed 3.3 V-compatible driver inputs**, not directly to lamps, EL wire, relay coils, or motor wiring. Verify outputs remain off during reset/boot with the driver circuits; firmware sets off levels before enabling the pins, but GPIO is high-impedance before firmware starts. Add external bias resistors if the drivers lack defined off-state bias.

## Voltage and power checks before connection

1. Keep motor and lighting supplies disconnected while rewiring. The controller uses its own local supply; do not enable PoE power simultaneously without checking the board/module's power arrangement. USB-C can power/program the board. If using the VSYS header (pin 39), verify the board's specified input range and your supply first. VBUS pin 40 is the USB rail, not an arbitrary supply input.
2. The encoder is described as **5 V-powered NPN open collector**. Retain its 5 V supply and ground. Connect a **4.7 kΩ resistor from each A/B signal to 3.3 V**, provided no encoder/module pull-up drives that signal to 5 V. Measure high levels before attaching the ESP32. If outputs have internal 5 V pull-ups or are actually push-pull, remove/replace pull-ups where supported or use a suitable level interface. ESP32 GPIO is not 5 V tolerant.
3. **OLED/Tic pull-up voltage remains unverified.** Check both modules for pull-ups to their supply rails. SDA/SCL must rise only to 3.3 V at the ESP32. Use a compatible 3.3 V OLED supply and pull-up arrangement, or an appropriate bidirectional I²C level shifter. The Tic motor supply remains unchanged; changing I²C logic voltage does not imply changing its motor power. Fit bus pull-ups only if absent; avoid stacking many module pull-ups in parallel.
4. Limit and beam outputs were confirmed as dry contacts to ground. Firmware uses internal 3.3 V pull-ups. Long/noisy wiring may require suitable external pull-ups, filtering, and cable routing; verify actual operation on the bench.
5. Retain the independent motor, encoder, and lighting supplies. Check the common **signal** ground arrangement; do not route motor load current through the ESP32 ground wiring or power rails.

## Reserved connections

Leave W5500 GPIO9–14 alone: reset 9, interrupt 10, MOSI 11, MISO 12, clock 13, chip select 14. Do not use USB GPIO19/20 (header pins 2/1), GPIO0/3/45/46 boot straps, GPIO26/33–37 flash/PSRAM connections, SD GPIO4–7, or the onboard RGB GPIO8. GPIO38–41 share camera signals; **do not attach a camera** in this installation.

The board supports GPIO interrupts. This firmware instead uses PCNT, preserving the old count scale: **600 counts = 1600 microsteps**. Default travel 18,600 microsteps corresponds to roughly 6,975 encoder counts. Confirm that opening increases counts; do not blindly swap encoder channels if it does not. Verify A/B wiring, rotation, and sensor specifications with motor power inhibited.
