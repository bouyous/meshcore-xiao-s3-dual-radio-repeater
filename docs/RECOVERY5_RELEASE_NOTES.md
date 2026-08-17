# recovery.5-alerts — experimental pre-release

This is an **experimental test release**, not a qualified field release.

## New in recovery.5

- Encrypted private MeshCore alerts after complete startup, at the 3.35 V
  pre-shutdown threshold, and immediately before LoRa/SYSTEMOFF shutdown.
- The supplied public key is seeded as the first recipient on a new install.
- Persistent CLI-managed list of up to four public-key recipients.
- Persistent `alert threshold` configuration, defaulting to 3350 mV with a
  3400 mV rearm threshold.
- ACK tracking and one retry for every recipient. Final shutdown allows a
  12-second radio grace period and preempts lower-priority pending messages.
- Existing winter daily GPS, persistent summer GPS, event journal and battery
  recovery behavior remain enabled.

## Validation completed

- Alert-recipient validation/list tests: 5/5 passed with Clang 22.
- Battery state-machine tests: 11/11 passed with Clang 22.
- Both the SYSTEMOFF/LPCOMP field profile and accelerated 3.45 V test profile
  compile successfully with the nRF52840 target toolchain.
- Final images use about 13.6% RAM and 50.6% application flash.

## Validation still required

- Confirm the supplied companion identity can decrypt and display `alert test`.
- Confirm ACK reception and retry behavior over the real mesh route.
- Measure voltage sag during the 3.35 V and final 3.30 V LoRa transmissions.
- Complete repeated autonomous discharge, solar recharge and wake cycles.
- Measure the actual field-profile LPCOMP wake threshold on each unit.

See the [French recovery.5 CLI guide](./SENSECAP_P1_CLI.md) before testing.
