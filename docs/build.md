# Build

The stable binary was built with Zig as a static ARMv5 executable.

Example on Windows PowerShell:

```powershell
$zig = "C:\path\to\zig.exe"
& $zig cc `
  -target arm-linux-musleabi `
  -mcpu=arm926ej_s `
  -static `
  -Os `
  -s `
  src\shm_rtsp.c `
  -o sdcard\shm_rtsp
```

The same command pattern can build diagnostics:

```powershell
& $zig cc -target arm-linux-musleabi -mcpu=arm926ej_s -static -Os -s tools\shm_watch.c -o shm_watch
& $zig cc -target arm-linux-musleabi -mcpu=arm926ej_s -static -Os -s tools\shm_types.c -o shm_types
```

Upload to the camera over FTP:

```powershell
curl.exe --fail -u root: -T sdcard\shm_rtsp ftp://CAMERA_IP:2121/mnt/shm_rtsp
```
