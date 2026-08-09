# Build, flash and recovery

## Build from source

```powershell
pio run -e Xiao_S3_WIO_dual_repeater
```

## Upload through PlatformIO

Close the MeshCore configurator and every serial monitor before uploading. Only one program can own the serial port.

```powershell
pio run -e Xiao_S3_WIO_dual_repeater -t upload --upload-port COM26
```

Replace `COM26` with the current Windows port.

## Flash the merged binary

The merged image contains the bootloader, partition table, boot application and firmware. Flash it at offset `0x0`:

```powershell
esptool.py --chip esp32s3 --port COM26 write_flash 0x0 firmware/MeshCore_Xiao_S3_WIO_dual_repeater_v1.17.0-dual.1-merged.bin
```

The non-merged application image belongs at offset `0x10000`:

```powershell
esptool.py --chip esp32s3 --port COM26 write_flash 0x10000 firmware/MeshCore_Xiao_S3_WIO_dual_repeater_v1.17.0-dual.1.bin
```

Do not write the application-only image at offset `0x0`.

## Expected boot log

```text
Dual SX1262 repeater mode
VALLEY radio init OK
BACKHAUL radio init OK
Repeater ID: <64-byte-public-key-in-hex>
```

## Recovery

If automatic reset fails:

1. Hold the XIAO `BOOT` button.
2. Tap `RESET` while keeping `BOOT` held.
3. Release `BOOT`.
4. Check the new COM port and repeat the flash command.

If Windows reports access denied, close the MeshCore web configurator, browser tabs, PlatformIO monitor and all terminal programs that may hold the port. Unplug and reconnect the XIAO if the lock remains.

## MeshCore configuration

Use [config.meshcore.io](https://config.meshcore.io/) in a Chromium-based browser. MeshCore name, region, frequency, SF, bandwidth, coding rate, password and location remain normal single-repeater settings shared by both RF ports.

After a fresh or merged-image flash, immediately replace the default administration password (`password`) before putting the repeater on the air. An application-only update normally preserves the existing identity and settings, but verify the repeater ID, RF profile and password after every flash.
