# Assembly tutorial with real photographs

[Version française](ASSEMBLY.fr.md)

This tutorial shows the actual prototype hardware: one **Seeed Studio XIAO ESP32-S3** and two **Seeed Studio Wio-SX1262 for XIAO** boards. The visual sheet summarizes the build; detailed steps follow below.

![Visual assembly tutorial](assets/assembly/assembly-tutorial-en.png)

## Parts

- 1 × Seeed Studio XIAO ESP32-S3;
- 2 × Seeed Studio Wio-SX1262 for XIAO;
- 2 × LoRa antennas matched to the operating band, or dummy loads for bench tests;
- two header rows for the `BACKHAUL` Wio;
- an insulated pigtail for an optional qualified rechargeable 3.7 V LiPo;
- multimeter, soldering iron, flux, magnifier and insulation materials.

![Actual prototype parts](assets/assembly/01-parts-overview.jpg)

## Essential `BACKHAUL` wiring map

The second Wio-SX1262 does not use the 30-pin B2B connector. It is fitted to the two XIAO side-header rows. Confirm this mapping before placing or soldering the board.

The `J1` side provides the independent controls:

| Wio | XIAO | GPIO | Purpose |
| --- | --- | ---: | --- |
| DIO1 | D0 | 1 | radio interrupt |
| BUSY | D1 | 2 | busy state |
| RESET | D2 | 3 | radio reset |
| NSS / CS | D3 | 4 | `BACKHAUL` SPI select |

The `J2` side provides power and the shared SPI bus:

| Wio | XIAO | GPIO |
| --- | --- | ---: |
| VIN | VIN / 5V | - |
| GND | GND | - |
| 3V3 | 3V3 | - |
| MOSI | D10 | 9 |
| MISO | D9 | 8 |
| SCK | D8 | 7 |

![Second Wio silkscreen labels](assets/assembly/04-backhaul-pin-labels.jpg)

## 1. Disconnect all power

Disconnect USB, battery and every other power source. Never solder a powered board. Fit an antenna or suitable dummy load to each Wio before any transmission test.

## 2. Prepare the battery before stacking

The battery pads are under the XIAO and become difficult to reach after the second Wio is fitted. Using USB-C as the orientation reference:

- `BAT-` is the pad nearest USB-C and connects to the black lead;
- `BAT+` is the pad away from USB-C and connects to the red lead.

Use a qualified rechargeable 3.7 V lithium battery. Insulate both joints and add strain relief. Do not connect the battery to `VIN`, `5V` or `3V3`.

![Official battery pads](assets/assembly/09-battery-pads.png)

## 3. Fit the `VALLEY` Wio

The first Wio-SX1262 uses Seeed's standard 30-pin B2B connector. Point the XIAO USB-C end upward, align both connectors and press vertically without twisting. This port is intended for the valley omnidirectional antenna.

![First Wio fitted through the B2B connector](assets/assembly/03-valley-radio-mounted.jpg)

## 4. Dry-fit the `BACKHAUL` Wio

Using the wiring map above, dry-fit the second Wio on the side headers. Both rows must stay parallel and the boards must not touch outside the intended connectors. Recheck the `J1` control side and `J2` power/SPI side before soldering.

## 5. Solder both rows

Hold the boards perfectly aligned. Solder one pin at each end first, check the alignment, then complete the remaining joints. Each joint should be clean, with no loose solder ball or bridge between adjacent pins.

![Stack viewed from above](assets/assembly/05-dual-stack-top.jpg)

![Stack viewed from the side](assets/assembly/06-dual-stack-side.jpg)

## 6. Check before applying power

With a multimeter:

- verify continuity of `GND`, `3V3`, `MOSI`, `MISO` and `SCK`;
- verify that `GPIO41` (the `VALLEY` NSS) is not connected to `GPIO4` (the `BACKHAUL` NSS);
- confirm there is no short between `3V3`, `VIN` and `GND`;
- check the battery pigtail polarity again.

## 7. Connect both antennas

Connect a suitable antenna to each U.FL connector: omnidirectional for `VALLEY`, directional for `BACKHAUL`. Do not let the small coaxial cables pull on the connectors. Antenna separation, polarization and RF isolation must be validated in the final enclosure.

![Both radios and their antennas](assets/assembly/02-parts-top.jpg)

## 8. Flash and verify

Follow the [flashing instructions](FLASHING.md), then check the boot log:

```text
Dual SX1262 repeater mode
VALLEY radio init OK
BACKHAUL radio init OK
```

The `get dualradio` command should then show both ports. Begin TX testing only after both antennas or dummy loads are connected.

The complete electrical mapping and assembly warnings remain available in the [wiring tutorial](WIRING.md).

## Image sources

Prototype photographs were taken by `bouyous`. Pinout diagrams come from Seeed Studio's official documentation for the [XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) and the [XIAO ESP32-S3 + Wio-SX1262 kit](https://wiki.seeedstudio.com/wio_sx1262_with_xiao_esp32s3_kit/).
