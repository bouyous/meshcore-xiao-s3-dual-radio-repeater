# Architecture

## One node, two RF ports

MeshCore sees one `mesh::Radio` implementation, one packet dispatcher and one cryptographic identity. `DualSX1262Wrapper` translates that logical interface into two physical SX1262 state machines.

This is why a MeshCore scan shows one repeater even though its advertisement is transmitted through two antennas.

## Receive path

Both enabled radios normally remain in RX mode. Each has an independent DIO1 interrupt flag.

When one frame is ready:

1. RadioLib checks and reads the frame.
2. Malformed or CRC-failed frames are discarded.
3. A path-independent FNV-1a signature is calculated from the stable MeshCore packet fields.
4. The ingress port and timestamp are retained for up to 60 seconds.
5. MeshCore parses the packet and applies its normal routing and duplicate rules.

If both radios complete the same frame in one loop iteration, both copies are read. The copy with better SNR, then RSSI, defines the ingress port. The other copy increments that port's duplicate counter and is not passed to MeshCore.

If the two radios complete different frames simultaneously, the `VALLEY` frame is returned first and the `BACKHAUL` frame is stored in a pending buffer for the next loop iteration.

## Transmit path

Before any TX:

1. Both radios enter standby.
2. The configured guard delay elapses.
3. Exactly one SX1262 begins transmitting.

For a forwarded packet, the stored ingress signature selects the transmission order:

| Ingress | First TX | Second TX |
| --- | --- | --- |
| `VALLEY` | `BACKHAUL` | `VALLEY` |
| `BACKHAUL` | `VALLEY` | `BACKHAUL` |

For locally generated traffic with no remembered ingress, `VALLEY` transmits first. Once the first transmission has completely finished, the guard delay elapses and the other port transmits the same frame. Both receivers return to RX only after the full sequence.

There is no simultaneous TX/TX or TX/RX operation.

Both physical transmissions use one MeshCore identity. Client applications that count acknowledgement events instead of unique public keys may display more than one "heard by" event for this single repeater.

## Loop protection

The design uses several independent layers:

1. SX1262/RadioLib CRC and receive-error rejection.
2. MeshCore packet parsing and length validation.
3. Five-second dual-radio duplicate suppression.
4. Sixty-second ingress-to-egress association.
5. MeshCore's standard 160-entry SHA-256-derived packet hash table.
6. MeshCore path and loop-detection logic.

The wrapper signature ignores mutable path bytes so that a packet still maps to its original ingress after MeshCore appends or removes path data.

## RF reality

Software arbitration prevents the two chips from transmitting at the same time, but it cannot create RF isolation. A nearby 22 dBm transmitter can heavily desensitize or overload another receiver front end even when the firmware is not asking it to decode.

The final installation should evaluate:

- antenna spacing and relative polarization;
- directional antenna front-to-back ratio;
- cable loss and connector quality;
- cavity, SAW or helical filtering where appropriate;
- coupled power at both SX1262 RF inputs;
- receiver recovery after TX;
- legal duty cycle and EIRP.

The 10 ms guard is a configurable software timing value, not a measured isolation specification. Both radios are unable to receive during this guard and during the complete LoRa transmission airtime. Crossing packets can therefore still collide or be missed; normal MeshCore retries and randomized delays reduce that risk but cannot eliminate it.
