# Known Working Device

This project was developed on one specific LookCam-style camera.

Known details:

```text
Chip: ANYKA AK3918FN080 V300
Kernel: Linux 4.4.192V2.1
Architecture: armv5tejl
Stock app: /usr/bin/anyka_ipc
H.264 shared memory size: 1048628
RTSP port used by this project: 554
SD mount: /mnt
SD startup hook: /mnt/usbnet/product_test
```

Network/debug entry points observed on the development unit:

```text
Telnet shell: 2323
FTP: 2121
Legacy telnet/debug: 23
```

Your camera may differ even if it looks identical externally.

Before assuming compatibility, check:

```sh
uname -a
cat /proc/sysvipc/shm
ls -l /dev/akpcm* /dev/video* 2>/dev/null
ps | grep '[a]nyka_ipc'
```
