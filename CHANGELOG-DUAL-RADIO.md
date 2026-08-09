# Dual-radio changelog

## v1.17.0-dual.2 - 2026-08-09

- disabled probing for absent external I2C RTC chips in the fixed dual-radio
  XIAO assembly;
- retained the ESP32 internal fallback clock;
- removed the roughly nine-second pre-radio startup delay and associated I2C
  timeout seen during the first `dual.1` hardware flash;
- documented that the existing ESP32 OTA path uses the local `MeshCore OTA`
  Wi-Fi access point; official MeshCore Bluetooth DFU applies to nRF52 boards.

## v1.17.0-dual.1 - 2026-08-09

- rebased the dual-SX1262 repeater on official MeshCore `repeater-v1.17.0`;
- adopted the 1.17 SX1262 preamble and payload receive timeouts on both radios;
- added MeshCore 1.17 hardware CAD support to both valley and backhaul ports;
- restored the 1.17 boolean RX boosted-gain result across both radios;
- preserved sequential dual-port transmission, persistent per-port configuration,
  the 10 ms inter-radio guard, French region handling, and fast boot.

## v1.16.0-dual.5 - 2026-07-11

- Skip external I2C sensor discovery on the fixed dual-radio repeater hardware.
- Remove the roughly two-minute boot delay caused by probing an unpopulated I2C bus.
- Keep the internal fallback clock, radio initialization, CLI and core repeater telemetry unchanged.

## v1.16.0-dual.4 - 2026-07-11

- Transmit every MeshCore-selected forwarded packet sequentially on both enabled RF ports.
- Transmit on the port opposite the ingress first, then on the ingress-side port.
- Include both physical transmissions in airtime estimation.
- Validate simultaneous dual-radio reception, duplicate coalescing and symmetric live-network forwarding.
- Validate an optional 220 uF supply capacitor through ten full-power serialized dual-radio TX cycles under USB power.
- Document that MeshCore clients may count multiple acknowledgement events for the single dual-radio identity.

## v1.16.0-dual.3 - 2026-07-10

- Reduce the default TX guard from 40 ms to 10 ms to shorten the receive-blind interval.
- Document crossing-packet risk during guard and LoRa airtime.
- Add XIAO underside battery-pad wiring to the tutorial diagram.

## v1.16.0-dual.2 - 2026-07-10

- Add persistent per-port power and enable controls.
- Add configurable inter-TX guard.
- Add per-port statistics.
- Add USB and remote command-path CLI integration.
- Coalesce simultaneous duplicate receptions using SNR/RSSI.
- Preserve a second simultaneous non-duplicate frame for the next loop.
- Prevent disabling both RF ports.
- Identify the board as `Xiao S3 WIO Dual SX1262`.

## v1.16.0-dual.1 - 2026-07-10

- Add one logical MeshCore radio backed by two SX1262 devices.
- Add shared-SPI initialization with independent NSS/DIO1/BUSY/RESET.
- Add opposite-port forwarding and sequential local TX on both ports.
- Add path-independent ingress signature tracking.
