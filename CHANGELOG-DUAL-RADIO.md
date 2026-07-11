# Dual-radio changelog

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
