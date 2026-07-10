# Development history

## 1. Goal

The project began with a practical coverage problem in mountainous terrain: use a directional antenna for an inter-summit link and an omnidirectional antenna for users in the valley, while exposing only one MeshCore repeater identity.

## 2. Raw-radio prototype

A small PlatformIO/RadioLib firmware was written before touching MeshCore. Its purpose was to validate the electrical design and isolate hardware problems from mesh-protocol problems.

The prototype established the standard Wio-SX1262 connection:

| Signal | XIAO ESP32-S3 GPIO |
| --- | ---: |
| SPI SCK | 7 |
| SPI MISO | 8 |
| SPI MOSI | 9 |
| DIO1 | 39 |
| BUSY | 40 |
| NSS | 41 |
| RESET | 42 |

It then validated the second Wio-SX1262 on the XIAO side headers:

| Signal | XIAO pin | GPIO |
| --- | --- | ---: |
| DIO1 | D0 | 1 |
| BUSY | D1 | 2 |
| RESET | D2 | 3 |
| NSS | D3 | 4 |
| SCK | D8 | 7 |
| MISO | D9 | 8 |
| MOSI | D10 | 9 |

The two radios share the SPI data lines, 3.3 V and ground. They have independent NSS, DIO1, BUSY and RESET lines.

## 3. Physical validation

Two otherwise identical XIAO/Wio assemblies were initially used as separate endpoints.

- Standard 30-pin Wio TX to side-header Wio RX: 6/6 packets, approximately -47 dBm RSSI and 11 dB SNR.
- Side-header Wio TX to standard 30-pin Wio RX: 6/6 packets, approximately -49 to -50 dBm RSSI and 11 dB SNR.
- Both Wio-SX1262 boards attached to one XIAO: both radios initialized and the raw bridge forwarded traffic to the opposite port without reported radio errors.

These were short-range bench tests. The values prove the digital wiring and RF operation, not long-range link performance.

## 4. MeshCore integration

The official MeshCore repository was checked out at tag `repeater-v1.16.0`, commit `07a3ca9e05b0ab23b878200b2c44b04e08131972`.

The existing RadioLib wrapper uses shared static interrupt state and is designed for one radio instance. A dedicated `DualSX1262Wrapper` was therefore added. It presents one `mesh::Radio` interface to MeshCore while managing two independent `CustomSX1262` instances.

The first implementation added:

- separate interrupt flags and state for each SX1262;
- shared SPI with independent chip-select lines;
- simultaneous idle-time RX;
- serialized TX;
- ingress-signature tracking that ignores mutable MeshCore path bytes;
- opposite-port forwarding;
- sequential dual-port TX for locally generated packets.

Holding both NSS pins high before either radio is initialized was necessary. Without that step, the unselected second board could disturb the shared SPI bus and the first radio returned initialization error `-2`.

## 5. MeshCore validation

The first MeshCore dual-radio firmware produced the following boot result:

```text
Dual SX1262 repeater mode
VALLEY radio init OK
BACKHAUL radio init OK
Repeater ID: CDDE90DD24319EC45A46D6FFCEA4E715C4A9D1667D6386004CA19E9BCF187443
```

A zero-hop local advertisement produced two physical transmissions, one on each radio. A separate raw-radio receiver detected both copies over RF.

## 6. CLI and arbitration update

Versions `v1.16.0-dual.2` and `v1.16.0-dual.3` added:

- persistent per-port TX power;
- persistent per-port enable state;
- configurable TX guard interval;
- per-port RX, TX, duplicate and error counters;
- protection against disabling both ports;
- simultaneous duplicate coalescing using SNR and RSSI;
- a pending frame buffer for the case where the two radios finish receiving different packets at the same time;
- integration with the common MeshCore command handler for USB and authenticated remote CLI paths.

The final bench configuration was restored to:

```text
VALLEY enabled=1 tx=22 dBm
BACKHAUL enabled=1 tx=22 dBm
guard=10 ms
```

Power values are firmware settings, not permission to exceed local EIRP rules.

## 7. Collaboration note

This was a personal hardware project developed interactively with OpenAI ChatGPT/Codex. The owner assembled and manipulated the hardware and antennas. ChatGPT/Codex inspected upstream code and schematics, generated the firmware changes, operated the local build and flash tools, and helped interpret serial and RF test results.
