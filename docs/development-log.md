# Anyka LookCam RTSP Handoff

## 2026-06-26 v14 update

- Built `work\zigtest\shm_rtsp_v14` from `work\zigtest\shm_rtsp.c`.
- Deployed to camera as `/mnt/shm_rtsp`; previous v13 saved as `/mnt/shm_rtsp_v13.bak`.
- Current `/mnt/shm_rtsp` size: `34644` bytes.
- RTSP server is listening on `0.0.0.0:554`.
- After a 15-second RTSP-over-TCP packet test from PC: `879` RTP packets, max packet gap about `164 ms`, `0` read timeouts.
- v14 changes:
  - `RING_END` now uses full shared memory size `SHM_BYTES` (`1048628`), matching payload offset `52` plus `1048576` bytes of ring data.
  - NAL lengths are measured around the circular buffer instead of as only linear byte ranges.
  - RTP/H.264 packetization can send a NAL split across the physical end/start of the ring buffer.
  - SDP SPS/PPS extraction uses ring-aware copying too.

## 2026-06-26 v15/v16 update

- v15 was deployed, then replaced by v16.
- Current camera binary: `/mnt/shm_rtsp` v16, size `34692` bytes.
- Backups on camera:
  - `/mnt/shm_rtsp_v13.bak` size `34276`
  - `/mnt/shm_rtsp_v14.bak` size `34644`
  - `/mnt/shm_rtsp_v15.bak` size `34716`
- v15 fixed v14's wrong ring end: `RING_END` is back to `1048576`; payload starts at `52`, so the usable ring is `52..1048576`.
- v16 keeps all valid raw H.264 NAL types `1..23` as boundaries, but still sends only video/SPS/PPS types currently used by the RTSP stream. This avoids corrupting a frame by merging it through filler/prefix/AUD-like NALs such as types `12` and `14`.
- After v16 deploy: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- 20-second RTSP-over-TCP packet test after v16: `1720` RTP packets, max packet gap about `149 ms`, `0` read timeouts.

## 2026-06-26 v17 update

- User reported after v16: periodic stalls are gone; square/block artifacts remain but are reduced.
- Screenshot showed block corruption during motion, consistent with damaged H.264 reference chain rather than network stalls.
- v17 deployed as current `/mnt/shm_rtsp`, size `34692` bytes.
- v16 saved as `/mnt/shm_rtsp_v16.bak`, size `34692`.
- v17 changes:
  - all found start-code blocks are used as NAL boundaries again, including private/invalid-looking type `0` blocks seen shortly before SPS/PPS/IDR;
  - only valid H.264 NAL types `1..23` are sent to RTP;
  - VCL NALs `1..5` set the RTP marker and advance timestamp;
  - non-VCL NALs such as SPS/PPS/SEI/AUD/filler/prefix are sent without marker so they do not look like frame ends.
- After v17 deploy: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- 20-second RTSP-over-TCP packet test after v17: `1301` RTP packets, `199` marker packets, max packet gap about `192 ms`, `0` read timeouts.

## 2026-06-26 v18/v19 update

- User reported v17 did not help; image still sometimes becomes heavily blocky/striped.
- v18 was deployed briefly, then replaced by v19.
- v18 finding: RTP analysis showed impossible fragmented control NALs (`FU-7`, `FU-8`, `FU-9`), meaning bogus/oversized SPS/PPS/AUD-like blocks were being sent to VLC.
- Current camera binary: `/mnt/shm_rtsp` v19, size `34892` bytes.
- v18 saved as `/mnt/shm_rtsp_v18.bak`, size `34732`.
- v19 behavior:
  - all start-code blocks still act as boundaries;
  - VCL `1`/`5` can be large and are packetized normally;
  - SPS/PPS/AUD/SEI are only sent if their sizes look sane (`SPS <= 256`, `PPS <= 128`, `AUD <= 32`, `SEI <= 4096`);
  - skipped blocks still advance `last_sent` so they do not get reprocessed forever.
- After v19 deploy: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- 20-second RTP type analysis after v19: `2326` RTP packets, `204` marker packets, max gap about `201 ms`, `0` read timeouts.
- v19 RTP types looked clean: `FU-1=2004`, `FU-5=304`, `NAL-6=1`, `NAL-7=8`, `NAL-8=9`, no bad `FU-7/FU-8/FU-9`.

## 2026-06-26 v20 update

- User showed v19 still sometimes produces severe block/stripe corruption.
- Added diagnostic `/mnt/shm_types` built from `work\zigtest\shm_types.c`; it only reads shared memory and prints NAL type/length stats.
- Diagnostic showed normal-looking H.264 layout in shared memory:
  - mostly `type 1` P frames, large `type 5` IDR frames;
  - small SPS/PPS (`type 7` around `38` bytes, `type 8` around `4` bytes);
  - recurring private/invalid `type 0` blocks around `25` bytes before SPS/PPS/IDR;
  - occasional `type 2` of length `1`, likely a false/private boundary, not useful video.
- Current camera binary: `/mnt/shm_rtsp` v20, size `34932` bytes.
- v19 saved as `/mnt/shm_rtsp_v19.bak`, size `34892`.
- v20 adds `HOLD_BACK_NALS 4`: do not send the last 4 NALs before the write pointer. This increases latency slightly but avoids reading frames too close to active encoder writes, which may produce torn/incomplete frames.
- After v20 deploy: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- 20-second RTP type analysis after v20: `1480` RTP packets, `209` marker packets, max gap about `202 ms`, `0` read timeouts.
- v20 RTP types looked clean: `FU-1=1213`, `FU-5=254`, `NAL-7=7`, `NAL-8=6`, no bad FU types.

## 2026-06-26 v21 update

- User showed v20 still has heavy block/stripe artifacts.
- Added diagnostic source `work\zigtest\shm_rate.c`, but its first telnet run did not return useful output; no server changes depended on it.
- Current camera binary: `/mnt/shm_rtsp` v21, size `34932` bytes.
- v20 saved as `/mnt/shm_rtsp_v20.bak`, size `34932`.
- v21 changes:
  - `HOLD_BACK_NALS` increased from `4` to `8`;
  - per-VCL sleep reduced from `65000 us` to `10000 us` via `FRAME_SLEEP_US`;
  - goal is to keep a safer distance from the encoder write head while allowing the RTSP server to drain any backlog quickly instead of falling behind and breaking P-frame reference chains.
- After v21 deploy: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- 20-second RTP type analysis after v21: `1484` RTP packets, `193` marker packets, max gap about `465 ms`, `0` read timeouts.
- v21 RTP types remained clean: `FU-1=1158`, `FU-5=311`, `NAL-7=7`, `NAL-8=7`, `NAL-9=1`, no bad FU types.

## 2026-06-26 v22 update

- User reported v21: video is a bit jerky; low-motion scenes are acceptable, but waving a hand / changing a large area still causes colorful block corruption.
- Current camera binary: `/mnt/shm_rtsp` v22, size `35380` bytes.
- v21 saved as `/mnt/shm_rtsp_v21.bak`, size `34932`.
- v22 adds a minimal H.264 slice-header parser:
  - strips emulation-prevention bytes in a small RBSP prefix;
  - reads `first_mb_in_slice`;
  - sets RTP marker/timestamp on the end of an access unit rather than blindly on every VCL NAL.
- v22 keeps v21 pacing settings: `HOLD_BACK_NALS=8`, `FRAME_SLEEP_US=10000`.
- After v22 deploy: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- 20-second RTP type analysis after v22: `2045` RTP packets, `210` marker packets, max gap about `496 ms`, `0` read timeouts.
- v22 RTP types remained clean: `FU-1=1706`, `FU-5=323`, `NAL-7=8`, `NAL-8=8`, no bad FU types.
- Marker count stayed close to v20/v21, so the stream may mostly be one VCL NAL per frame; if v22 does not improve visual corruption, next useful diagnostic is an IDR-only mode to prove whether P-frame/reference handling is the remaining failure.

## 2026-06-26 v23 update

- User reported v22 is much better: sometimes `20-30 sec` without artifacts, only rare `1-2` corrupted frames, and a roughly `1 sec` stall every `20-30 sec`.
- Current camera binary: `/mnt/shm_rtsp` v23, size `35380` bytes.
- v22 saved as `/mnt/shm_rtsp_v22.bak`, size `35380`.
- v23 keeps v22 access-unit marker logic.
- v23 pacing changes:
  - `FRAME_SLEEP_US` changed from `10000` to `80000`;
  - added `RTP_TS_INC 9000` instead of hardcoded timestamp step `6000`;
  - goal: match observed stream rate around `10 fps` instead of advertising/playing as `15 fps`, and avoid burst-then-wait behavior.
- After v23 deploy: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- 20-second RTP analysis after v23: `1866` RTP packets, `212` marker packets, max gap about `134 ms`, `0` read timeouts.
- v23 RTP types remained clean: `FU-1=1465`, `FU-5=385`, `NAL-7=8`, `NAL-8=8`, no bad FU types.
- RTP timestamp delta over the 20-second sample was `1908000` ticks, matching roughly `21.2 sec` of 90 kHz RTP time; this is much closer to live pacing than the old `6000` tick step.
- User visual result after v23: excellent; more than `2 min` without major artifacts. Remaining issue is rare small-area corruption for `1-2` frames when the scene changes after being static for a long time / a new object enters the frame.
- Recommended current stable version: v23. Next optional tuning, only if desired, is a small latency tradeoff such as increasing holdback slightly (`HOLD_BACK_NALS 10-12`) to see whether the rare transition-frame corruption disappears.

## 2026-06-26 v24 update

- v24 was tested and then rolled back because user reported it was worse than v23.
- Current camera binary after rollback: `/mnt/shm_rtsp` v23, size `35380` bytes.
- v23 saved as `/mnt/shm_rtsp_v23.bak`, size `35380`.
- Rejected v24 saved as `/mnt/shm_rtsp_v24.bad`, size `35380`.
- v24 keeps v23 access-unit marker and pacing logic.
- v24 only changes `HOLD_BACK_NALS` from `8` to `12`, adding a little latency to reduce rare transition-frame corruption.
- After v24 deploy: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- 20-second RTP analysis after v24: `1848` RTP packets, `185` marker packets, max gap about `139 ms`, `0` read timeouts.
- v24 RTP types remained clean: `FU-1=1503`, `FU-5=330`, `NAL-1=1`, `NAL-7=7`, `NAL-8=7`, no bad FU types.
- After rollback to v23: one `/mnt/shm_rtsp` process, RTSP listening on `0.0.0.0:554`.
- Final recommended stable version: v23 (`HOLD_BACK_NALS=8`, `FRAME_SLEEP_US=80000`, `RTP_TS_INC=9000`, access-unit marker logic enabled).

## 2026-06-26 stable SD kit

- Goal: keep the solution SD-based. With the SD card inserted, `/mnt/usbnet/product_test` starts the local RTSP server; without the SD card, the internal firmware is untouched and the camera boots normally.
- Deployed new SD `product_test` to `/mnt/usbnet/product_test`, size `2314`.
- Previous SD script saved as `/mnt/usbnet/product_test.old_v10`, size `1334`.
- Current SD RTSP binary `/mnt/shm_rtsp` is stable v23, size `35380`.
- Stable binary backup saved as `/mnt/shm_rtsp_v23.final`, size `35380`.
- The new `product_test`:
  - starts telnet/FTP debug services only if their ports are not already listening;
  - keeps stock `/usr/bin/anyka_ipc` running and does not kill it;
  - waits for H.264 shared memory size `1048628`;
  - starts `/mnt/shm_rtsp`;
  - runs a watchdog loop that restarts only `shm_rtsp` if port `554` stops listening.
- Syntax check on camera passed: `/bin/sh -n /mnt/usbnet/product_test`.
- Created recovery archive on PC: `C:\Users\user\Documents\Codex\2026-06-26\new-chat\outputs\anyka_sd_rtsp_v23_stable.zip`
- Archive contents:
  - `shm_rtsp`
  - `usbnet/product_test`
  - `README.txt`
  - `docs/anyka_rtsp_handoff.md`
- Archive SHA256: `5E231832A0D1BE132668CB2030FDCC9EAE99ADE73B80AC213DC18CD8FA8268B6`

Краткое состояние проекта на 2026-06-26.

## Цель

Получить локальный RTSP-поток с китайской Wi-Fi камеры LookCam на Anyka AK3918FN080 V300 без облака.

## Камера

- IP: `192.168.0.63`
- MAC: `40-9C-A7-D8-FA-8C`
- В роутере видна как: `network device`
- Чип: `ANYKA AK3918FN080 V300`
- Linux: `4.4.192V2.1`, `armv5tejl`
- Сенсор/ISP конфиг: `/etc/config/isp_h63p_mipi_1lane_h3b.conf`
- Родное приложение: `/usr/bin/anyka_ipc`
- SD-карта на камере: `/mnt`

## Точки входа

- Telnet root shell: `192.168.0.63:2323`
- Старый telnet/debug: `192.168.0.63:23`
- FTP/root без пароля: `ftp://root:@192.168.0.63:2121/`
- Текущий RTSP: `rtsp://192.168.0.63/`
- RTSP порт: `554`

Пример проверки FTP с ПК:

```powershell
curl.exe -u root: ftp://192.168.0.63:2121/mnt/
```

## Что уже получилось

- Найден и включен debug/product-test механизм через SD-карту.
- Получен root-доступ.
- Выяснено, что родной `anyka_ipc` сам создает готовый H.264 в shared memory.
- Готовые RTSP-хаки Teckin/Kalkan/VGerris не подошли из-за несовместимости uClibc/ISP/sensor layer.
- Установлен Zig на ПК и подтвержден запуск статических ARMv5-musl бинарей на камере.
- Собран свой минимальный RTSP-over-TCP сервер `shm_rtsp`.
- Сервер читает H.264 из SysV shared memory `shmid 0`, отвечает на RTSP и отдает RTP/H.264.
- VLC/клиент уже показывает картинку.

## Текущее состояние на камере

- На камере лежит и запущен: `/mnt/shm_rtsp`
- Текущая версия бинаря: `v13`
- Размер текущего `/mnt/shm_rtsp`: `34276` байт
- Автозапуск: `/mnt/usbnet/product_test`
- Старый product-test сохранен как: `/mnt/usbnet/product_test.prev`
- Родное приложение `anyka_ipc` должно оставаться запущенным, иначе нет живого H.264 и пропадают часы/OSD.
- Текущий autostart не должен убивать `anyka_ipc`; он только поднимает debug-сервисы, ждет shared memory и запускает `/mnt/shm_rtsp`.

## Shared memory

Ключевое:

- `shmid 0`, размер `1048628` байт: основной H.264 поток.
- В начале есть служебный заголовок, payload начинается примерно с offset `52`.
- Размер кольца фактически `1048576` байт.
- В заголовке есть движущиеся pointer-like поля:
  - offset `4`: двигается около текущей записи.
  - offset `8`: тоже около текущей записи, похож на write/head pointer.
  - offset `12`: указывает на свежий GOP/keyframe area и меняется реже.
- H.264 NAL types:
  - `7` SPS
  - `8` PPS
  - `5` IDR/keyframe
  - `1` обычные video frames

## Текущая проблема

RTSP уже работает, но видео все еще периодически затыкается:

- было: дергание и зацикливание;
- после `v13`: стало лучше;
- сейчас: примерно `7 секунд нормально`, потом `1 секунда зависания`, потом снова нормально.

Вероятная причина:

NAL/H.264 кадр пересекает границу кольцевого буфера. `v13` уже идет по кольцу и не повторяет старые куски, но еще может считать последний физический NAL перед концом памяти “целым кадром”, хотя его хвост лежит в начале кольца. Нужно в `v14` собирать wrapped NAL из двух частей: конец буфера + начало буфера до следующего start code.

## Локальные файлы

Рабочая папка:

```text
C:\Users\user\Documents\Codex\2026-06-25\ghb
```

Важное:

- `work\zigtest\shm_rtsp.c` - исходник текущего RTSP-сервера.
- `work\zigtest\shm_rtsp_v13` - текущий собранный бинарь v13.
- `work\zigtest\shm_watch.c` - диагностика заголовка shared memory.
- `work\zigtest\shm_probe.c` - поиск H.264 start codes в shared memory.
- `work\zigtest\shm_dump.c` - дамп shared memory.
- `work\sd_rtsp_shm_v10\product_test` - локальный autostart-скрипт, его стоит переименовать/почистить под v13/v14.
- Zig: `work\tools\zig\zig-x86_64-windows-0.16.0\zig.exe`

## Как собирать

```powershell
$zig = "C:\Users\user\Documents\Codex\2026-06-25\ghb\work\tools\zig\zig-x86_64-windows-0.16.0\zig.exe"
& $zig cc -target arm-linux-musleabi -mcpu=arm926ej_s -static -Os -s work\zigtest\shm_rtsp.c -o work\zigtest\shm_rtsp_v14
```

## Как заливать новую версию

Через FTP:

```powershell
curl.exe --fail -u root: -T work\zigtest\shm_rtsp_v14 ftp://192.168.0.63:2121/mnt/shm_rtsp_v14
```

Потом через telnet `2323`:

```sh
killall shm_rtsp
mv /mnt/shm_rtsp_v14 /mnt/shm_rtsp
chmod +x /mnt/shm_rtsp
/mnt/shm_rtsp >> /mnt/log/shm_rtsp.log 2>&1 &
netstat -ltn | grep ':554'
```

## Следующий шаг

Сделать `v14`:

- не отправлять NAL как линейный кусок до `RING_END`, если следующий NAL хронологически находится в начале кольца;
- добавить отправку wrapped NAL: `[payload..RING_END) + [RING_BEGIN..next_start)`;
- желательно не отправлять самый свежий недописанный NAL около write pointer;
- после деплоя проверить VLC по `rtsp://192.168.0.63/`.

## Чего не повторять

- Не выключать/убивать `anyka_ipc`: без него нет видеопотока и могут пропасть часы.
- Не тратить время на Kalkan v200 RTSP: упирается в несовместимость ISP/sensor config.
- Не тратить время на Teckin/VGerris готовые бинарники без пересборки: на этой прошивке проблемы с libc/libpthread/libm/libdl/`__libc_fork`/FPE.
- Не включать облако как обязательную часть решения: цель локальный RTSP из уже готового H.264 в памяти.
