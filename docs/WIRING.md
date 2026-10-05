# Wiring migration

Header positions below are verified against [Waveshare's official pin diagram](https://docs.waveshare.com/assets/images/ESP32-S3-ETH-details-15-b9d2e97a5122db37da6be5bac19e29b4.webp), linked from [the board documentation](https://docs.waveshare.com/ESP32-S3-ETH). View the **component side**, USB-C at the top and microSD at the bottom: physical pin 1 is top right, pin 20 bottom right, pin 21 bottom left, and pin 40 top left. GPIO numbers are different from physical header positions.

## Both doors

- **I²C SDA → GPIO41, physical pin 10.** Previously NodeMCU D2 / GPIO4, as actually passed to `Wire.begin(D2,D1)` in the old code. Its SDA/SCL comment was reversed. Connect to the **3.3 V / LV SDA side of the existing I²C level shifter**.
- **I²C SCL → GPIO40, physical pin 11.** Previously NodeMCU D1 / GPIO5. Connect to the **3.3 V / LV SCL side of the level shifter**.
- **Encoder A → GPIO48, physical pin 4.** Previously NodeMCU D5 / GPIO14.
- **Encoder B → GPIO47, physical pin 5.** Previously NodeMCU D6 / GPIO12. A rising with B low counts positive; B high counts negative. These adjacent pins are on the same header as SDA/SCL; physical pin 3 provides nearby signal ground.
- **Closed limit contact → GPIO21, physical pin 35.** Previously MCP23008 pin 0. Other contact goes to signal ground. Closed/active pulls low.
- **Beam sensor contact → GPIO1, physical pin 25.** Previously MCP23008 pin 1. Other contact goes to signal ground. Broken/active pulls low.
- **Signal ground → physical pin 28 or 33.** Other header grounds are 3, 8, 13, 18, 23, and 38. Connect controller, encoder, Tic signal ground, OLED, sensor contacts, and driver input grounds appropriately.
- **3.3 V → physical pin 36.** Use for the level-shifter LV reference and low-side I²C pull-ups. Encoder A/B use internal pull-ups for the initial trial; this rail is also available for external pull-ups if needed. Do not use 3V3_EN (pin 37) as a power rail.

Tic address remains **0x0E** (device number 14); OLED is **0x3C**, SSD1306 128×64. Match the Tic's device-number and I²C settings with the Pololu configuration utility. Retain the **MCP23008 at address 0x20**, powered from the existing 5 V logic supply. A0/A1/A2 remain grounded. Keep its RESET pin held high as in the existing circuit; it must not float. The register driver follows [Microchip’s MCP23008 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23008-and-MCP23S08-Data-Sheet-DS20001919.pdf). MCP23008 GP0/GP1 are now unused inputs because the limit and beam contacts move to the ESP32 GPIOs above.

## Level-shifted I²C bus

- **ESP32 side (3.3 V):** GPIO41 → LV SDA; GPIO40 → LV SCL; board 3.3 V → LV reference. Pull-ups on this side go to 3.3 V.
- **Peripheral side (5 V):** HV reference → existing regulated 5 V logic supply. HV SDA/SCL → MCP23008 SDA/SCL and Tic T500 SDA/SCL. Pull-ups on this side go to 5 V. Retain the existing bidirectional I²C level shifter.
- **Common signal ground:** ESP32, shifter, 5 V logic supply, MCP23008, Tic, and OLED share signal ground. Retain expander supply decoupling.
- **OLED:** its voltage compatibility and existing placement still need checking. A 3.3 V-compatible OLED can connect on the LV bus; only place it on the HV bus if its power and signal ratings support 5 V. Keep address 0x3C unique.

The 5 V peripheral SDA/SCL wires must never connect directly to GPIO41/40. The Tic's motor supply and motor wiring remain unchanged. Do not power the expander from ESP32 3V3 or join the 5 V supply to the 3.3 V rail.

## Front door

Retain these existing MCP connections. **GP numbers below mean expander ports, not ESP32 GPIO or IC package pin numbers.**

- **Up button contact → MCP GP6.** Other side to ground. Internal MCP pull-up enabled; pressed is LOW.
- **Down button contact → MCP GP4.** Other side to ground. Internal MCP pull-up enabled; pressed is LOW.
- **Up acceptance driver input → MCP GP7.** HIGH is active; LOW is off.
- **Down acceptance driver input → MCP GP5.** HIGH is active; LOW is off. Preserved behavior leaves this light off.

Button presses are reported in web/USB diagnostics but remain inactive for motion and QLab, matching the original firmware. The up light turns on when closed and clears on opening.

## Rear door

- **First EL-wire driver input → MCP GP6.** LOW is active; HIGH is off.
- **Second EL-wire driver input → MCP GP7.** LOW is active; HIGH is off.

Retain the existing 5 V expander-to-driver connections; MCP ports drive the **existing driver inputs**, not lamps, EL wire, relay coils, or motor wiring directly. Firmware sets off levels before enabling the expander outputs. During power-up/reset or communication failure, software cannot guarantee those levels: verify the existing driver bias gives a defined off state when MCP outputs are high-impedance. If a powered expander becomes unreachable, its previous output latch can remain active until hardware reset/power removal.

Buttons and lighting drivers connect only to the MCP23008. ESP32 GPIO41/40 now carry I²C SDA/SCL. GPIO38/39 (physical pins 14/12) and the former I²C GPIO17/18 (physical pins 34/31) are unused by this firmware.

## Voltage and power checks before connection

1. Keep motor and lighting supplies disconnected while rewiring. The controller uses its own local supply; do not enable PoE power simultaneously without checking the board/module's power arrangement. USB-C can power/program the board. If using the VSYS header (pin 39), verify the board's specified input range and your supply first. VBUS pin 40 is the USB rail, not an arbitrary supply input.
2. Keep the encoder's confirmed **5 V supply** and common ground. A/B are described as NPN open collector with **no internal encoder pull-ups**. For the initial trial, connect A/B directly to GPIO48/47 and **omit external resistors**. Firmware explicitly enables the ESP32's internal 3.3 V pull-ups after PCNT initialization. Before attaching A/B, verify the encoder does not drive them HIGH to 5 V; once firmware is running, rotate slowly and confirm signals alternate between approximately 0 V and 3.3 V. The internal pull-ups are weak (typically about 45 kΩ), so verify accurate counts with the actual cable length, motor running, and maximum configured speed. If counts are missed or noisy, add **one external 4.7 kΩ resistor from each A/B signal to 3.3 V**; firmware may retain its internal pull-ups. A/B must not connect to the 5 V I²C bus or its pull-ups. ESP32 GPIO is not 5 V tolerant.
3. **The MCP23008 and Tic use the existing level-shifted 5 V bus.** Verify idle SDA/SCL measure approximately 3.3 V on LV and 5 V on HV before attaching the ESP32. Confirm the OLED supply, pull-ups, and bus-side placement separately. Fit bus pull-ups only if absent; avoid stacking many module pull-ups in parallel.
4. Limit and beam outputs were confirmed as dry contacts to ground. Firmware uses internal 3.3 V pull-ups. Long/noisy wiring may require suitable external pull-ups, filtering, and cable routing; verify actual operation on the bench.
5. Retain the independent motor, encoder, and lighting supplies. Check the common **signal** ground arrangement; do not route motor load current through the ESP32 ground wiring or power rails.

## Reserved connections

Leave W5500 GPIO9–14 alone: reset 9, interrupt 10, MOSI 11, MISO 12, clock 13, chip select 14. Do not use USB GPIO19/20 (header pins 2/1), GPIO0/3/45/46 boot straps, GPIO26/33–37 flash/PSRAM connections, SD GPIO4–7, or the onboard RGB GPIO8. GPIO38–41 share camera signals; **do not attach a camera** in this installation.

The board supports GPIO interrupts. This firmware instead uses PCNT, preserving the old count scale: **600 counts = 1600 microsteps**. Default travel 18,600 microsteps corresponds to roughly 6,975 encoder counts. Confirm that opening increases counts; do not blindly swap encoder channels if it does not. Verify A/B wiring, rotation, and sensor specifications with motor power inhibited.
