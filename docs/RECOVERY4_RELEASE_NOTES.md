# recovery.4 — experimental pre-release

This is an **experimental test release**, not a qualified field release.

## Highlights

- Persistent summer mode: `gps override on`.
- Persistent winter mode: `gps override off`.
- Temporary GPS acquisition overrides: `gps override 1h` through `168h`.
- Low-battery protection always has priority over GPS.
- Persistent events for override start/end, GPS fixes and power transitions.
- Both 3.45 V accelerated-test and SYSTEMOFF/LPCOMP field images.

## Validation completed

- 27/27 host policy tests passed.
- Both SenseCAP Solar P1 environments build successfully.
- Generic XIAO nRF52 repeater regression build passed.
- DFU manifests and UF2 family/address were checked.
- Event journal persistence was previously checked on a real P1 across reboot.

## Validation still required

- Full autonomous discharge, shutdown, solar recharge and wake cycle.
- Actual LPCOMP rising threshold per unit and over temperature.
- Long-duration GPS override power consumption and fix performance.
- Mountain winter operation and charger NTC cutoff behavior.

See [the French recovery.4 CLI guide](./SENSECAP_P1_CLI.md) before testing.
