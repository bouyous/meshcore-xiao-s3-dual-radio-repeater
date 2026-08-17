# XIAO ESP32-S3 battery measurement and recovery

The Seeed Studio XIAO ESP32-S3 does not route the battery voltage to an ADC.
Seeed documents that battery voltage cannot be read in software without an
external connection. The recovery firmware therefore requires the following
measurement circuit for both the single-radio and dual-radio builds.

## Required circuit

Use 1% resistors and keep the wiring short:

```text
BAT+ ---- 1.0 MOhm ----+---- D4 / GPIO5
                       |
                     330 kOhm
                       |
BAT- / GND ------------+---- GND

100 nF ceramic capacitor: D4 / GPIO5 to GND
```

Never connect `BAT+` directly to a GPIO. With a 4.20 V battery, the expected
voltage on D4 is approximately 1.04 V. Verify this with a multimeter before
powering the XIAO. The divider draws approximately 3.2 microamps at 4.2 V.

The firmware uses the nominal divider factor `(1,000,000 + 330,000) / 330,000
= 4.030303`. The standard MeshCore ADC multiplier command can be used to
calibrate a specific unit against a trusted multimeter.

## Pin allocation

- `D4 / GPIO5`: battery ADC; unavailable for I2C in these recovery builds.
- `D6 / GPIO43`: I2C SDA if a custom build re-enables sensors.
- `D7 / GPIO44`: I2C SCL if a custom build re-enables sensors.
- `D0..D3 / GPIO1..4`: second SX1262 control pins in the dual-radio build.

Sensor and RTC auto-discovery are disabled in the published repeater images.

## Power policy

- median ADC sample every 30 seconds;
- warning and channel alert at 3.60 V;
- critical state below 3.50 V;
- shutdown only after 10 continuous minutes below the critical threshold;
- 12-second radio grace period for the final alert and retry;
- ESP32-S3 deep sleep with each SX1262 held reset and deselected;
- battery check every 5 minutes;
- normal boot resumes at or above 3.70 V;
- impossible ADC readings are ignored during normal operation so an open or
  unsettled input cannot falsely shut down a running repeater.

During recovery sleep, pressing the XIAO reset button clears the retained
recovery state and provides a physical service path if the divider wiring must
be repaired.

## OTA image

Use the application-only `firmware.bin` asset for an OTA Wi-Fi update. Do not
use a merged factory image in the OTA form. An application-only update normally
preserves the MeshCore identity, radio preferences and alert-channel files;
verify them after every update.

## Official hardware reference

- [Seeed Studio XIAO ESP32-S3 battery documentation](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/#battery-usage)
