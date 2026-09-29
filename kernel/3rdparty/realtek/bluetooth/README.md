# Realtek RTL8761CU Bluetooth firmware

Vendor firmware for the ASUS Bluetooth dongle `0b05:1bf6` (an RTL8761CU, measured). It is not our
work and not built here: the two `.bin` files are byte-for-byte copies from linux-firmware and must
stay unmodified — the licence allows redistribution in binary form **without modification** only, and
**only with `LICENCE.rtlwifi_firmware.txt` beside them**. Licence record: `LICENSE.md`.

| File | Size | md5 | git blob |
|---|---|---|---|
| `rtl8761cu_fw.bin` | 10296 | `02fe0df6ff0736a96321e24d1f306539` | `475320d0bd5c982cc472422cb37d263f785a24f3` |
| `rtl8761cu_config.bin` | 11 | `65bbb0c01ddc2005081e1e2684331f6a` | `018dcf93b1cbad5e865dfe6f27598382c90b4973` |

**Provenance — measured 2026-09-29.** linux-firmware
(`https://gitlab.com/kernel-firmware/linux-firmware`) commit `35e542439e8aaca81fa1527fe3887afbb68a6779`,
*"rtl_bt: Add firmware and config files for RTL8761CUV"* (2025-10-29). It is the only commit that has
touched either file, and the blob IDs above are the ones that commit records. The licence is the one its
`WHENCE` names for the `rtl_bt/` section, copied from the same commit.

## Hints

- **Where it goes on the device:** `/lib/firmware/rtl_bt/rtl8761cu_fw.bin` and `…_config.bin`. The
  directory name is part of the name the driver requests; the files do nothing anywhere else.
- **Who asks for it:** `btrtl.ko`, and only with `kernel/patches-modules/btrtl-rtl8761cu.patch` applied.
  Stock 4.14 picks firmware by `lmp_subver` alone and asks this chip for `rtl8761a_fw.bin`, the wrong
  chip's file. The patch keys on `hci_rev 000e`. `btusb.ko` needs `btusb-asus-1bf6-realtek.patch` to
  hand `0b05:1bf6` to btrtl at all. Both modules come from `kernel/build-bt-modules.sh`.
- **How to tell it worked:** `dmesg` shows `rtl: examining hci_ver=0d hci_rev=000e … lmp_subver=8761`
  and then a request for `rtl_bt/rtl8761cu_fw.bin`. After the download the HCI version
  read reports revision `0x7bf1` instead of `0x000e`, manufacturer 93 (Realtek) — measured on .188.
- **Missing firmware is not fatal to the kernel,** but the adapter stays unusable. A missing
  `_config.bin` alone is tolerated (config size 0).
- **Updating:** take both files from one newer linux-firmware commit, replace them together, and
  rewrite the table and the provenance paragraph above from a measurement: `md5sum`,
  `git hash-object`, and the commit list for `rtl_bt/rtl8761cu_fw.bin`. Re-copy the licence file from
  the same commit; if `WHENCE` names a different licence, the `LICENSE.md` row changes too.
- **Do not gzip them.** Git already compresses what it stores, and a compressed copy breaks a direct
  md5 comparison with the file on the device.
