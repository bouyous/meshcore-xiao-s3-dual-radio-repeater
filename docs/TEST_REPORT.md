# Bench test report

Date: 10-11 July 2026

## Hardware under test

- Seeed Studio XIAO ESP32-S3
- Two Seeed Studio Wio-SX1262 for XIAO boards
- USB serial port `COM26` for the final dual-radio assembly
- A second XIAO/Wio assembly used during raw RF validation

## Source baseline

- MeshCore tag: `repeater-v1.16.0`
- Upstream commit: `07a3ca9e05b0ab23b878200b2c44b04e08131972`
- Variant: `Xiao_S3_WIO_dual_repeater`
- Firmware version string: `v1.16.0-dual.5`
- Final RF profile read from `COM26`: `869.6179809 MHz`, `62.5 kHz` bandwidth, spreading factor `8`, coding rate `8`

## Build result

```text
RAM:   59,792 / 327,680 bytes (18.2%)
Flash: 1,136,585 / 3,342,336 bytes (34.0%)
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

The final `dual.5` state was restored to `22/22 dBm`, both enabled, guard `10 ms`.

## Sequential TX validation

After clearing statistics and issuing `advert.zerohop`, the completed result was:

```text
stats-packets: sent=2, direct_tx=1
VALLEY:   tx=1
BACKHAUL: tx=1
```

This confirms one logical MeshCore advertisement and two serialized physical transmissions.

## Live MeshCore traffic validation

A companion node named `Test` transmitted at 2 dBm while the nearby roof repeater was moved temporarily to 916 MHz. The dual repeater remained on its normal test channel. A first isolated test produced:

```text
VALLEY:   rx=3, tx=2, errors=0
BACKHAUL: rx=3, tx=2, errors=0
MeshCore: recv=3, sent=4, flood_rx=2, flood_tx=2
```

A second mixed test sent eight operations from two clients: two messages per client, one zero-hop alert per client and one network alert per client. The result was:

```text
VALLEY:   rx=8, tx=3, errors=0
BACKHAUL: rx=8, tx=3, errors=0
MeshCore: recv=8, sent=6, flood_rx=6, direct_rx=2, flood_tx=3
```

The two zero-hop packets were received but correctly not forwarded. Every packet selected by MeshCore for forwarding generated one serialized TX on each physical port. Duplicate receptions were coalesced and no radio receive errors were reported. After adding and allowing the `fr` transport region, traffic from the second client was also forwarded.

The client UI may report multiple "heard by" events for this node while listing only one repeater identity. This is expected when the same logical packet and its acknowledgements traverse both physical RF ports under one MeshCore public key.

## 3.3 V rail load test with optional capacitor

An optional used `220 uF / 10 V` electrolytic capacitor was installed between `3V3` and `GND` on the accessible side-header Wio-SX1262. The capacitor is not required by the design; this test checks that the value is tolerated and that it can provide additional transient decoupling.

With the XIAO powered by USB, both radios enabled at `22 dBm` and the inter-TX guard set to `10 ms`, ten zero-hop advertisements were issued one at a time. Each logical advertisement produced two serialized physical transmissions:

```text
VALLEY:        tx=10, errors=0
BACKHAUL:      tx=10, errors=0
stats-packets: sent=20, direct_tx=10, recv_errors=0
Firmware after test: v1.16.0-dual.4
```

No spontaneous restart, radio error or asymmetric TX count was observed. A deliberately over-fast command burst filled the MeshCore outbound queue and accepted fewer transmissions; this was a software queue limit, not evidence of supply failure. The test validates USB-powered operation but does not yet validate operation from a nearly discharged Li-ion cell. An oscilloscope measurement of the 3.3 V rail remains the correct way to quantify short voltage dips.

## Fast-boot validation (`dual.5`)

The previous build scanned every external I2C address despite this fixed repeater having no external I2C sensors. With an unpopulated bus, per-address timeouts delayed the CLI for roughly two minutes and emitted repeated `Error 263` messages. `dual.5` disables only that external sensor-discovery call for the dual-radio build.

After flashing `dual.5`, a reset-to-CLI measurement returned `ver` after `1778 ms`. Both radios initialized before the reply, the MeshCore identity and persisted settings were preserved, and a zero-hop advertisement produced one TX on each port with zero radio errors.

## Firmware hashes

```text
3A60D118E5D01BC13A064CAC6D3C05C6994B7F8082B7C36631881DF51809CD08  application image
A3698D3E36F99819D82FAAE404FD86DFE76FC79283601B372FEF24CE75B60E7A  merged image
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
