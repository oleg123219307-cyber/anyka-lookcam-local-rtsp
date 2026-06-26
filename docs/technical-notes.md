# Technical Notes

## Device

The known working target:

- Anyka `AK3918FN080 V300`
- Linux `4.4.192V2.1`
- ARM `armv5tejl`
- Stock app: `/usr/bin/anyka_ipc`
- H.264 shared memory: SysV `shmid 0`, size `1048628`
- SD path: `/mnt`

## Shared Memory

The stock application creates a H.264 ring buffer in shared memory.

Important values used by `src/shm_rtsp.c`:

```c
#define SHMID 0
#define SHM_BYTES 1048628
#define RING_BEGIN 52
#define RING_END 1048576
```

The first `52` bytes are a header. The rest behaves as a circular H.264 payload area.

Observed H.264 NAL types:

- `1`: non-IDR video slice
- `5`: IDR/keyframe slice
- `7`: SPS
- `8`: PPS
- `0`: private/non-H.264 boundary-like block seen before GOP headers

The final implementation treats all start-code blocks as boundaries, but only sends sane H.264 NALs to RTP.

## Why the Marker Logic Matters

Early versions marked every VCL NAL as a separate RTP frame. Under motion, that caused major block corruption.

The stable server parses a small prefix of the H.264 slice header and uses `first_mb_in_slice` to decide access-unit boundaries. This fixed the large "colored block" artifacts on the tested camera.

## Stable Tuning

The final stable tuning was:

```c
#define HOLD_BACK_NALS 8
#define FRAME_SLEEP_US 80000
#define RTP_TS_INC 9000
```

`HOLD_BACK_NALS=12` was tested and was worse on the development camera, so the repository keeps `8`.

## Audio

Audio is not implemented.

The camera exposes Anyka PCM devices such as:

```text
/dev/akpcm_adc0
/dev/akpcm_dac0
/dev/akpcm_loopback0
```

Audio was intentionally left out because video stability was the priority and touching audio capture risked disturbing the working pipeline.

## Diagnostics

The `tools/` directory contains small read-only diagnostics used during development:

- `shm_watch.c`: prints shared-memory pointer/header and nearby NALs
- `shm_types.c`: prints NAL type/length statistics

Build them with the same target as the RTSP server.
