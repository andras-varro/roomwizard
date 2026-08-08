# Licence

This project's own code is **MIT**. Everything it links against, builds from or redistributes is
enumerated below, because two of those things carry obligations that MIT does not.

**Why MIT and not Apache-2.0.** This repository publishes binaries that are **GPL-2.0-only** — the three
kernel modules under *usb\_host* — and Apache-2.0's patent-termination clause is incompatible with
GPL-2.0-only. MIT is compatible with GPL-2.0, GPL-2.0-or-later and GPL-3.0-or-later, all three of which
appear below. That is the whole reason for the choice.

**What is not here, deliberately.** No Steelcase firmware and no device configuration. `uImage-system` is
a 5.2 MB vendor binary and is never copied, never committed and never published — the USB 500 mA patch is
*derived* on the spot from the copy already on the device, gated on its md5. `release.sh` refuses to
publish either class of file rather than trusting each component to remember
([`IMPROVEMENT_PLAN.md` F15](IMPROVEMENT_PLAN.md)).

---

## MIT License

Copyright (c) 2026 Andras Varro

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## What MIT covers

Everything committed in this repository **except the one file named in the next section**: the C sources
and shared library under `native_apps/`, `vnc_client/`'s client and its renderer/input/settings, the
ScummVM backend port under `scummvm-roomwizard/backend-files/`, `usb_host/`'s `devmem_write.c` and Python
tooling, every shell script (`roomwizard.sh`, `deploy-all.sh`, `release.sh`, `lib/`, `commissioning/`,
`device-files/`, `tests/`), the `.ppm` launcher icons, and the documentation.

⚠️ **MIT covers the *source*; it does not decide the licence of a *binary* it is linked into.** The
backend port under `scummvm-roomwizard/backend-files/` is MIT as source and is compiled into ScummVM, so
the resulting `scummvm` binary is GPL-3.0-or-later as a whole. Same for `vnc_client`, which links
LibVNCClient. Both are listed under *Distributed binaries* below. Taking the backend port's source and
using it elsewhere is governed by MIT alone.

---

## Third-party code committed in this repository

Exactly one file, and it is worth knowing about precisely because it is the only one:

| File | Licence | What it is |
|---|---|---|
| `scummvm-roomwizard/vkeybd_roomwizard.zip` | **GPL-3.0-or-later** | A 2× scaled derivative of ScummVM's own `vkeybd_small.zip` virtual-keyboard pack, produced by the committed `make_vkeybd_scaled.py`. Derived from ScummVM, so it carries ScummVM's licence, not this project's. |

Nothing else third-party is committed. Every other dependency is either **downloaded at build time** or
**built from a tree the developer clones themselves**, and neither is redistributed by this repository.

---

## Built from sources this repository does not contain

Cloned or downloaded by the build scripts, into a local prefix. Listed with the versions the scripts
actually pin, because "current upstream" is not a fact anyone can check later.

| Component | Version | Licence | Fetched by |
|---|---|---|---|
| ScummVM | whatever the developer clones into `scummvm/` (gitignored) | **GPL-3.0-or-later** | manual `git clone`; `scummvm-roomwizard/build-and-deploy.sh` builds it |
| LibVNCServer / LibVNCClient | 0.9.14 | **GPL-2.0-or-later** | `vnc_client/build-deps.sh` |
| libjpeg-turbo | 2.1.5.1 | IJG · BSD-3-Clause · zlib | `vnc_client/build-deps.sh` |
| zlib | 1.2.13 (`vnc_client`), 1.3.1 (ScummVM) | zlib licence | both build scripts |
| libpng | 1.6.43 | PNG Reference Library License v2 | `scummvm-roomwizard/build-and-deploy.sh` |
| Linux kernel | 4.14.52, unmodified upstream | **GPL-2.0-only** | `usb_host/build-xpad-module.sh`; the tree lives at `usb_host/linux-4.14.52/` (gitignored) |
| glibc | whatever `arm-linux-gnueabihf-gcc` provides | LGPL-2.1-or-later | the distribution's cross toolchain |

⚠️ **These are cross-compiled from source on purpose, not vendored.** This host cannot do armhf
multiarch, so each component builds its own copies
([`SYSTEM_ANALYSIS.md` §6](SYSTEM_ANALYSIS.md#6-building-for-this-device)). The consequence for this file
is that *nothing here is redistributed as source by this repository* — the obligations attach to the
**binaries** a release publishes, which is the next section.

---

## Distributed binaries, and the obligations they carry

A release bundle (`./release.sh`) publishes ARM binaries. Its `NOTICE` file — generated by `release.sh`
and shipped inside every tarball — is the per-release half of this; **this file is the repo-level half,
and the two must agree.**

| Published artifact | Effective licence of the binary | Obligation |
|---|---|---|
| `scummvm` | **GPL-3.0-or-later** | Corresponding source: upstream ScummVM plus `scummvm-roomwizard/backend-files/`. Written offer in `NOTICE`. |
| `scummremastered.zip`, `gui-icons.dat` | **GPL-3.0-or-later** | ScummVM's own theme and GUI data, staged verbatim from the ScummVM tree. Same offer. |
| `vkeybd_roomwizard.zip` | **GPL-3.0-or-later** | Derivative of ScummVM's `vkeybd_small.zip` (see above). Same offer. |
| `vnc_client` | **GPL-2.0-or-later** | Links LibVNCClient. Corresponding source: LibVNCServer 0.9.14, zlib, libjpeg-turbo, plus this repository. |
| `xpad.ko`, `joydev.ko`, `ff-memless.ko` | **GPL-2.0-only** | Kernel modules. ⚠️ **Written source offer required**, and it is in `NOTICE`: unmodified upstream Linux 4.14.52 from kernel.org, plus `usb_host/build-xpad-module.sh` and the device's own kernel configuration. No kernel source is modified — these are upstream drivers rebuilt for this kernel's config. |
| `devmem_write`, the `native_apps` games and tools, `app_launcher` | MIT, statically linked against glibc (LGPL-2.1-or-later) | LGPL-2.1 §6 relinking: the objects and this repository are what a relink needs. |

The three `.ko`s are the reason the choice of licence for this project's own code mattered at all: a
GPL-2.0-**only** binary in the same distribution is what rules Apache-2.0 out.

---

## No warranty, and one hardware note

The MIT text above disclaims warranty, and that disclaimer is meant literally here. Two specifics:

- **This project writes to the SD card's boot partition by default.** `commissioning/provision.sh` and
  `commissioning/commission-offline.sh` both patch `uImage-system` to raise the USB power budget from
  100 mA to 500 mA. The vendor image is backed up to `uImage-system.vendor` on the same partition first,
  and the backup's md5 is verified before the original is touched — but **a power cycle is no longer a
  free undo**. In-place remedy: copy the backup back. Fallback: pull the card and restore it.
  `--no-usb-power` opts out.
- **Recovering a unit that will not boot means reaching the SD card, and that means opening the case.**
  It is feasible and it takes experience; an inexperienced attempt can break the enclosure. Nobody
  associated with this project is responsible for a broken case, a broken card or a bricked unit.

Steelcase and RoomWizard are trademarks of their respective owner. This project is unaffiliated with,
and not endorsed by, Steelcase.
