# anyka-lookcam-local-rtsp
<<<<<<< HEAD
Local SD-card RTSP server for Anyka/LookCam AK3918 cameras, bypassing cloud video using the stock H.264 shared memory stream
=======

Local SD-card RTSP server for Anyka/LookCam AK3918 cameras, bypassing cloud video by re-streaming the stock H.264 shared-memory stream.

This is a practical recovery/hack kit for cheap Anyka-based Wi-Fi cameras where the vendor app/cloud is unwanted, broken, or untrusted. It keeps the original camera application running and exposes its already-encoded H.264 video as a local RTSP stream.

Tested on:

- LookCam-style Wi-Fi camera
- Anyka `AK3918FN080 V300`
- Linux `4.4.192V2.1`
- H.264 shared memory segment size `1048628`
- SD-card product-test hook at `/mnt/usbnet/product_test`

## Current State

Working:

- Local RTSP video at `rtsp://<camera-ip>/`
- SD-card based startup
- No firmware flashing required
- Safe fallback: remove the SD card to boot stock behavior
- Watchdog restarts the RTSP server if port `554` stops listening

Not implemented:

- Audio
- ONVIF
- Authentication
- Multiple streams

## Safety Model

This project intentionally avoids writing to the camera firmware. The camera root filesystem is read-only squashfs on this device, and the working integration point is the SD card:

- With the prepared SD card inserted, `/mnt/usbnet/product_test` starts the local RTSP server.
- Without the SD card, the camera should boot normally.
- The stock `/usr/bin/anyka_ipc` process must stay running, because it owns the ISP/sensor pipeline and writes the H.264 stream.

Do not kill `anyka_ipc` unless you know exactly how to recover the camera.

## Quick Install

1. Format an SD card as FAT/FAT32.
2. Copy the contents of `sdcard/` to the root of the SD card.
3. The SD root should contain:

   ```text
   shm_rtsp
   usbnet/product_test
   ```

4. Insert the SD card into the camera.
5. Power-cycle the camera.
6. Find the camera IP in your router.
7. Open:

   ```text
   rtsp://<camera-ip>/
   ```

For the test camera used during development:

```text
rtsp://192.168.0.63/
```

## What the SD Script Does

`sdcard/usbnet/product_test`:

- starts telnet/FTP debug services if they are not already listening;
- keeps the stock `/usr/bin/anyka_ipc` process running;
- waits for the H.264 SysV shared memory segment;
- starts `/mnt/shm_rtsp`;
- watches port `554` and restarts only `shm_rtsp` if needed.

Logs are written under `/mnt/log/`.

## Building

The checked-in binary `sdcard/shm_rtsp` is the stable ARMv5 static build used on the test camera. To rebuild it yourself with Zig:

```powershell
$zig = "C:\path\to\zig.exe"
& $zig cc -target arm-linux-musleabi -mcpu=arm926ej_s -static -Os -s src\shm_rtsp.c -o sdcard\shm_rtsp
```

The same target was used for the working build:

```text
arm-linux-musleabi
arm926ej_s
static
```

## Implementation Notes

The server:

- attaches to SysV shared memory `shmid 0`;
- reads the H.264 ring buffer starting at offset `52`;
- uses ring-aware NAL extraction and RTP/H.264 FU-A packetization;
- uses H.264 access-unit marker logic rather than marking every VCL NAL as a frame;
- uses conservative pacing tuned for the tested camera:

```c
HOLD_BACK_NALS = 8
FRAME_SLEEP_US = 80000
RTP_TS_INC = 9000
```

The final stable version is the v23 logic described in `docs/development-log.md`.

## Troubleshooting

Check SD files:

```sh
ls -l /mnt/shm_rtsp /mnt/usbnet/product_test
```

Check that the stock app is alive:

```sh
ps | grep '[a]nyka_ipc'
```

Check RTSP:

```sh
ps | grep '[s]hm_rtsp'
netstat -ltn | grep ':554'
```

Check shared memory:

```sh
cat /proc/sysvipc/shm
```

Expected H.264 segment on the tested device:

```text
size 1048628
```

## Recovery

If the SD card breaks:

1. Format a new SD card as FAT/FAT32.
2. Copy `sdcard/shm_rtsp` to the SD root.
3. Copy `sdcard/usbnet/product_test` to `usbnet/product_test` on the SD.
4. Insert and power-cycle.

If something behaves badly, remove the SD card or rename `usbnet/product_test`.

## Warning

This is a device-specific hack. Cheap Anyka cameras often ship with different sensors, firmware layouts, libc versions, and debug hooks. Use at your own risk.

This project is useful for the tested AK3918FN080 V300 LookCam-style firmware, but it is not a universal Anyka RTSP solution.
>>>>>>> ee322d7 (Add SD-card RTSP server kit)
