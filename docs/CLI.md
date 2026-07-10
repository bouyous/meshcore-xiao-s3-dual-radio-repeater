# Dual-radio CLI

All replies fit the existing MeshCore 160-byte CLI reply buffer.

## Inspect configuration

```text
get dualradio
get valley
get backhaul
```

Example:

```json
{"valley":{"enabled":1,"tx":22},"backhaul":{"enabled":1,"tx":22},"guard_ms":40}
```

## Inspect statistics

```text
stats valley
stats backhaul
```

Example:

```json
{"port":"VALLEY","rx":0,"tx":1,"dup":0,"errors":0,"rssi":0,"snr":0.0}
```

`rx` counts valid physical receptions on the selected port. `tx` counts completed physical transmissions. `dup` counts frames discarded because the same logical packet was already received. `errors` counts RadioLib receive errors.

Reset the dual-radio counters:

```text
clear dualradio.stats
```

The standard MeshCore `clear stats` command also resets these counters.

## Set power

Set one port independently:

```text
set valley tx 14
set backhaul tx 22
```

Accepted range: `-9..22 dBm`.

Set both ports together through the standard MeshCore command:

```text
set tx 18
```

Power changes are rejected while a packet is actively transmitting. Wait for the current transmission to finish and retry.

## Enable or disable a port

```text
set valley enabled on
set valley enabled off
set backhaul enabled 1
set backhaul enabled 0
```

The firmware refuses to disable the last enabled port. This prevents accidental loss of all RF access.

Disabling the port used by a remote administrator can prevent the reply from returning on that link. Perform topology changes over USB when possible.

## Set the TX guard

```text
set dualradio guard 10
```

Accepted range: `10..1000 ms`. The guard is applied before TX and between the two sequential transmissions of a locally generated packet.

## Restore defaults

```text
reset dualradio
```

This enables both ports, copies the current common MeshCore TX power to both ports and restores the compile-time guard default of 10 ms.

## Help

```text
dualradio help
```

## Shared radio profile

Frequency, bandwidth, spreading factor and coding rate intentionally remain shared:

```text
get radio
set radio <frequency>,<bandwidth>,<sf>,<cr>
```

Independent RF profiles would turn this project into a cross-channel bridge and require different scheduler and protocol semantics.
