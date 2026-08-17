# SenseCAP Solar Node P1 runtime battery recovery

This target-specific extension is based on MeshCore repeater v1.17.0,
commit `727fc0512ce08bfd7b499e46daa7fca6eeec730d`.

## Behaviour

- Battery voltage is sampled every 30 seconds using a discarded conversion
  followed by a seven-sample median and a 40 us SAADC acquisition time.
- The GPS is enabled for the first hour after boot and then for one hour every
  24 hours. It is off for the other 23 hours.
- A local serial command can temporarily extend the GPS acquisition window or
  persistently select a seasonal mode. `gps override on` is the summer mode
  (continuous GPS); `gps override off` is the winter mode (daily one-hour
  window). Both choices survive resets and power loss. A timed override such
  as `gps override 96h` deliberately saves winter mode and returns to the
  daily schedule when its timer expires.
- The first valid fix in each daily window triggers one flood advertisement;
  the GPS parser also refreshes the RTC from a fresh fix.
- Below 3500 mV, the state becomes `economy` and the GPS schedule is suppressed.
- At or above 3550 mV, `normal` resumes. The GPS starts only if the current
  daily window is still open.
- Below 3300 mV, the state becomes `critical`.
- The critical timer is cleared only at or above 3350 mV.
- At or below 3350 mV, an encrypted pre-shutdown alert is sent to every
  configured public key while the node and LoRa radio are still fully active.
  This alert rearms after the battery reaches 3400 mV.
- After 600 seconds continuously in the critical band, GPS and LoRa are shut
  down. A final priority alert gets a 12-second radio grace period (one retry),
  then `LOW_VOLTAGE` is stored in GPREGRET2, LPCOMP/VBUS wake is armed and the
  nRF52840 enters SYSTEMOFF.
- After a complete boot, an encrypted startup alert reports measured voltage,
  reset cause and the previous shutdown reason.
- USB/external power suppresses runtime low-voltage shutdown.
- Invalid ADC readings outside 1000..5000 mV cannot directly request shutdown;
  a physically possible deeply discharged battery remains critical.

The power-state values are compile-time overrides in
`variants/sensecap_solar/variant.h`. Alert recipients and the 3350/3400 mV
alert hysteresis are persistent and can be changed through the authenticated
CLI without rebuilding the firmware.

## Voltage wake limitation

The official XIAO nRF52840 Plus battery divider is 1 MOhm / 499 kOhm on AIN7.
The available LPCOMP steps do not include an exact 3.60 V battery threshold.
The selected 3/8 VDD reference is the only practical step and gives a nominal
rising threshold around 3.77 V including typical comparator hysteresis. Device,
resistor and regulator tolerances make a bench measurement mandatory.

A requested 3.45 V System OFF threshold cannot be selected in firmware with
this divider. The next lower LPCOMP step is 5/16 VDD, approximately 3.15 V at
the battery including typical hysteresis. It is below the 3.30 V shutdown point
and can cause immediate wake or a low-voltage boot loop, so this build does not
use it. An exact test threshold needs either a resistor/reference modification
or a higher-consumption System ON polling mode that would not validate the real
System OFF recovery path.

The separate `SenseCap_Solar_repeater_test345` profile deliberately implements
that temporary polling mode: after the normal 3.30 V / 10 minute shutdown
decision it switches off LoRa, GPS and LEDs, sleeps in low-power SYSTEM ON,
samples VBAT every 30 seconds and resets after three consecutive readings at or
above 3.45 V. It is intended only to accelerate solar testing. The field profile
continues to use true SYSTEMOFF and LPCOMP at the nominal ~3.77 V threshold.

`VBAT_ENABLE` (P0.14) uses the nRF52840 S0D1 open-drain mode. It is kept LOW
for voltage-recovery SYSTEMOFF, which follows Seeed's sink-only requirement,
keeps the divider available to LPCOMP and avoids over-voltage on P0.31. The
divider costs about 2.5 uA while voltage wake is armed; it is released to
high-impedance for user-requested shutdown.

## CLI diagnostics

```text
get power.state
get power.vbat
get power.lowtime
get power.wakethreshold
get power.status
get power.temp
get power.chargeguard
get gps.schedule
gps override status
gps override 24h
gps override 96h
gps override on
gps override off
alert status
alert threshold
alert test
```

Timed values from 1 to 168 hours are accepted. The override only opens the GPS
acquisition window; a low-battery economy or critical state still forces the
GPS off and therefore always has priority. `on` and `off` are stored in a
separate one-byte LittleFS marker so changing season does not rewrite the main
MeshCore preferences. These commands are deliberately local-serial only.

Operational alert commands (`alert list`, `alert get`, `alert add`,
`alert remove`, `alert clear`, `alert threshold`, and `alert test`) are also
accepted over an authenticated MeshCore administrator CLI session. The list
holds up to four 32-byte public keys. Messages are individually encrypted;
unknown routes use encrypted flood delivery and known ACL routes use direct
delivery.

The existing commands remain available:

```text
get pwrmgt.support
get pwrmgt.source
get pwrmgt.bootreason
get pwrmgt.bootmv
```

## Persistent operational event log

The repeater profiles keep the latest 64 operational transitions in
`/p1_events.bin`. Each fixed-size record has a sequence number, boot ID,
uptime, two event values and a CRC. An interrupted write is ignored at the next
boot without invalidating older slots. Only transitions are written, not the
30-second voltage samples, to limit flash wear.

Recorded events include:

- boot, reset reason, preceding shutdown reason and boot voltage;
- successful completion of initialization;
- watchdog/CPU lockup and detectable radio/sensor initialization failures;
- normal/economy/critical transitions, final shutdown and test-profile solar
  recovery reset;
- GPS detection, daily window start/end, first fix, acquisition time, satellite
  count and RTC synchronization, plus GPS override start/end and duration;
- a no-fix GPS window as an explicit error-marked event.

The fallback clock is not treated as UTC. Entries first carry uptime only. A
later valid GPS timestamp anchors the same boot session, and earlier entries
are displayed with an estimated UTC timestamp marked by `~`. Implausible NMEA
dates are rejected. Commands are local-serial only:

```text
eventlog
eventlog status
eventlog clear
```

The filesystem is mounted before radio initialization so a radio failure can
be persisted. A failure to mount the filesystem itself cannot be written to
that same filesystem; it is printed as a fatal serial error instead. A boot
that never reaches `BOOT_COMPLETE` also remains visible in the journal.

## Mandatory bench validation

Use a current-limited programmable supply in place of the battery and keep a
known recovery path (UF2 bootloader or SWD). Do not connect a Li-ion pack while
sweeping the supply.

1. Confirm normal boot and LoRa/BLE/CLI operation around 3.8 V.
2. Briefly dip below 3.3 V and confirm no shutdown.
3. Hold below 3.3 V for 10 minutes and confirm a clean SYSTEMOFF.
4. Increase voltage very slowly and record the actual autonomous wake voltage.
5. Confirm `get pwrmgt.bootreason` reports LPCOMP and Low Voltage.
6. Repeat at the temperature extremes that the electronics (not the cells) can
   safely withstand.

The installed pack NTC is connected to the CN3165 TEMP input. The CN3165
autonomously suspends charging outside the resistor-defined temperature window,
including while the MCU is in SYSTEMOFF. No documented P1/XIAO pin exposes this
NTC voltage to the nRF52840, so firmware must not present the MCU die temperature
as battery temperature or claim control over charging. `get power.temp` labels
the MCU value explicitly and `get power.chargeguard` reports the autonomous
hardware guard. A true firmware temperature policy requires the TEMP/NTC node
to be routed to a spare ADC input and its resistor/NTC curve to be known.

Official references:

- https://files.seeedstudio.com/wiki/XIAO-BLE/Seeed_Studio_XIAO_nRF52840_Plus_PDF.pdf
- https://wiki.seeedstudio.com/meshtastic_solar_node/
- https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/lpcomp.html
