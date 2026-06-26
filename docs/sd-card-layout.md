# SD Card Layout

Copy the contents of `sdcard/` to the root of a FAT/FAT32 SD card.

Expected result:

```text
SD root
|-- shm_rtsp
`-- usbnet
    `-- product_test
```

## Files

### `shm_rtsp`

Static ARMv5 RTSP server binary.

It reads the stock H.264 stream from Anyka shared memory and exposes it over RTSP on port `554`.

### `usbnet/product_test`

Startup hook used by this firmware when the SD card is inserted.

The script:

- starts debug telnet/FTP services if needed;
- keeps `/usr/bin/anyka_ipc` running;
- waits for the H.264 shared memory segment;
- starts `/mnt/shm_rtsp`;
- restarts `shm_rtsp` if port `554` stops listening.

## Fallback Behavior

The internal firmware is not modified.

If the SD card is removed, or `usbnet/product_test` is renamed/deleted, the camera should boot in its stock mode.

## Logs

Runtime logs are written to:

```text
/mnt/log/sd_rtsp_v23.log
/mnt/log/shm_rtsp.log
/mnt/log/anyka_ipc-sd.log
```
