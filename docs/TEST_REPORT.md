# Bench test report

Date: 10 July 2026

## Hardware under test

- Seeed Studio XIAO ESP32-S3
- Two Seeed Studio Wio-SX1262 for XIAO boards
- USB serial port `COM26` for the final dual-radio assembly
- A second XIAO/Wio assembly used during raw RF validation

## Source baseline

- MeshCore tag: `repeater-v1.16.0`
- Upstream commit: `07a3ca9e05b0ab23b878200b2c44b04e08131972`
- Variant: `Xiao_S3_WIO_dual_repeater`
- Firmware version string: `v1.16.0-dual.3`
- Final RF profile read from `COM26`: `869.6179809 MHz`, `62.5 kHz` bandwidth, spreading factor `8`, coding rate `8`

## Build result

```text
RAM:   59,792 / 327,680 bytes (18.2%)
Flash: 1,136,605 / 3,342,336 bytes (34.0%)
PlatformIO result: SUCCESS
```

## Raw-radio validation

| Direction | Result | Typical RSSI | Typical SNR |
| --- | --- | ---: | ---: |
| Standard 30-pin Wio to side-header Wio | 6/6 packets | -47 dBm | 11 dB |
| Side-header Wio to standard 30-pin Wio | 6/6 packets | -49 to -50 dBm | 11 dB |

## Final firmware boot

```text
Dual SX1262 repeater mode
VALLEY radio init OK
BACKHAUL radio init OK
Repeater ID: CDDE90DD24319EC45A46D6FFCEA4E715C4A9D1667D6386004CA19E9BCF187443
```

The identity matched the repeater identity before the `dual.2` update.

## CLI validation

Validated commands:

- `ver`
- `board`
- `get dualradio`
- `get valley`
- `get backhaul`
- `stats valley`
- `stats backhaul`
- `set valley tx <value>`
- `set backhaul tx <value>`
- `set valley enabled <value>`
- `set backhaul enabled <value>`
- `set dualradio guard <value>`
- standard MeshCore `set tx <value>`
- `clear dualradio.stats`
- `dualradio help`

Persistence test:

```text
Before reboot: VALLEY=21, BACKHAUL=20, guard=60
After reboot:  VALLEY=21, BACKHAUL=20, guard=60
Result: PASS
```

The final `dual.3` state was restored to `22/22 dBm`, both enabled, guard `10 ms`.

## Sequential TX validation

After clearing statistics and issuing `advert.zerohop`, the completed result was:

```text
stats-packets: sent=2, direct_tx=1
VALLEY:   tx=1
BACKHAUL: tx=1
```

This confirms one logical MeshCore advertisement and two serialized physical transmissions.

## Firmware hashes

```text
797BCA16DD0BA0395AB5975A9CD0511E79568704BFE53B5B626DD128BBC59D25  application image
DA1F4FEEAA07F6088A9C76532A90E880E9493632E9151D517BDB790593348A20  merged image
```

See [`../firmware/SHA256SUMS.txt`](../firmware/SHA256SUMS.txt).

## Not yet validated

- long-duration packet-load test;
- final antenna isolation and coupled-power measurement;
- directional mountain-to-mountain path;
- omnidirectional valley coverage;
- authenticated custom CLI commands over LoRa;
- behavior in a dense production MeshCore network;
- compliance measurements for the final antenna and power configuration.
