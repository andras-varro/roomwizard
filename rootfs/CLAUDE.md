# rootfs/ — our own p6 root filesystem and the card image

Everything that builds the **root filesystem we boot instead of the vendor's Yocto 3.1.4**: a Buildroot
`BR2_EXTERNAL` tree, the overlay, and the card-image scripts (`build-rootfs.sh`, `check-rootfs.sh`,
`fetch-card-parts.sh`, `make-card-image.sh`). Device facts go in `SYSTEM_ANALYSIS.md`, open work in
`IMPROVEMENT_PLAN.md`; neither is restated here. Booted and working on `.188`.

## Rules before the first edit

- **ABI: soft-float EABI (not EABIHF), loader `/lib/ld-linux.so.3`, glibc >= 2.31.** Measured 2026-10-08 by
  `readelf` of the vendor `libc.so.6` and `/opt/games/snake`: our dynamic binaries need at most `GLIBC_2.28`
  plus `libasound.so.2`, `libm`, `libstdc++`; BlueZ/bluealsa (`bluetooth/build-bluez.sh`, `build-bluealsa.sh`)
  also need glib, `libdbus-1.so.3` and readline 8. `vnc_client` and `devmem_write` are static hard-float.
- **Build: Buildroot 2025.02 LTS** (supported to 2028-03), out of tree in WSL's native fs (`~/`, not DrvFs).
  `BR2_cortex_a8` + `BR2_ARM_EABI` + NEON, internal glibc with C++, no kernel (`BR2_LINUX_KERNEL` off, 4.14
  headers), BusyBox init driving SysV `rcS.d`/`rc5.d` so provisioning's links run unchanged.
- ⚠️ **`BR2_PACKAGE_BASH` needs `BR2_PACKAGE_BUSYBOX_SHOW_OTHERS`; `make defconfig` drops it silently.**
  `build-rootfs.sh` gates it. Debootstrap is ruled out: no qemu-user/binfmt here, and generic ARMv7 packages
  may carry `sdiv` (Cortex-A8 has none). Gate the target tree with `native_apps/check-arm-safe.sh`.
- **The root stays p6 and p1's boot files stay untouched** (`mlo`, `u-boot.bin`, `ctrlblock.bin`; `root=` is
  compiled into U-Boot): `SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery`. Recovery is "reimage the card".
- **Our links:** `rcS.d` `S01mountall` `S01seedrng` `S02alignment` `S03udev` `S04hostname` `S05sysctl`
  `S06watchdog` `S07syslog` `S08networking`; `rc5.d` `S02dbus-1` `S09sshd` `S20hwclock.sh`.
  `device-files/clean-rules.conf` knows only the vendor's names.

## What the image must carry, and what it need not

- **Carry:** OpenSSH, not dropbear (`rw-sshd.sh` runs `sshd -t` and sets `Ciphers`/`MACs`/`KexAlgorithms`/
  `PubkeyAcceptedKeyTypes`); a `/dev/watchdog` feeder on every boot (60 s); udev with the rule that makes
  `/dev/input/touchscreen0` (`app_launcher` exits without it); dbus (+ `DBUS_SYSTEM_BUS_ADDRESS`), avahi,
  `amixer`, `libasound` **with `/usr/share/alsa`**; `insmod`/`lsmod`, `rdate`, `hwclock -u`, `date -s`,
  `logger` + a syslogd, `start-stop-daemon`, `pidof -x`, `killall`, `/usr/share/zoneinfo`, DHCP on eth0
  sending the host name, `reboot`/`poweroff` (BusyBox has no `shutdown` applet).
- **Need not:** cron, `S40ctrlblk`, `update-rc.d`, `/var/watchdog_test`, `disable-steelcase.sh`,
  `clean-rules.conf`. BlueZ and bluealsa are our own builds, not Buildroot packages.

## Per-unit state (carried by `make-card-image.sh`'s `state.tar`)

**Both lists live in `lib/rw-state.sh` and nowhere else**: `RW_STATE_IMAGE` (what `fetch-card-parts.sh`
puts in `state.tar`) and the BACKUP set (`commissioning/backup.sh` / `restore.sh`). ⚠️ **An image never
carries** `vnc_client.conf` (plaintext password), `scummvm.ini`, ScummVM saves or game data, loose
`/home/root` files, `touch_calibration.conf.bak*`, the vendor `/etc/hosts` or p2 contents:
`make-card-image.sh` runs `rw_state_image_deny` (an allowlist plus a hard list) over `state.tar` before any
image exists and refuses, never filters — an older `state.tar` fails and is re-fetched. It derives
`/etc/hosts` from the carried `/etc/hostname` with `commissioning/set-hostname.sh`. Highscores, host keys
(`/home/root/data/ssh`) and cron live on p2, which the image creates empty — `restore.sh` puts them back. ⚠️ **`config.c:65` saves by tmp + `rename()`, which replaces a
symlink instead of writing through it**, so a symlink farm onto p2 does not relocate that file. **SSH host
keys are generated on first boot** (the vendor's are identical across units, `SYSTEM_ANALYSIS.md#52-as-we-run-it--game-mode`),
so a fresh p2 means a slow first boot.

## Operator rulings

Buildroot; card-image install only; nothing from Steelcase on the card except `mlo`, `u-boot.bin`,
`u-boot-sd.bin`, `ctrlblock.bin`; p2 and p3 hold only ours; p5 is cleared (ext4, label `rw-spare`, unmounted,
not in `fstab`); a per-unit SSH identity is wanted; p7 swap stays off.
