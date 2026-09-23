# kernel/ — our 4.14.52 image, its patches and its drivers

Everything that goes **into the kernel image** lives here: the source patches, the device-tree changes,
the build script, and the out-of-tree driver work. The walkthrough and the working notes are
[`README.md`](README.md). Facts about the device, including what the vendor kernel does, go in
`SYSTEM_ANALYSIS.md` (policy and dropped-driver table: [§7](../SYSTEM_ANALYSIS.md#7-kernel-policy)).
Open work goes in `IMPROVEMENT_PLAN.md`. Neither is restated here.

## Rules before the first edit

- ⚠️ **Never build in, or patch, `usb_host/linux-4.14.52/`.** That tree is the *module* build's, and
  the modules are built from the **unpatched** source (`LICENSE.md` records that split). The image is
  built by `build-image.sh` in a fresh tree under WSL `$HOME`, never on `/mnt/c`, because DrvFs makes a
  kernel build crawl.
- **A patch here is GPL-2.0-only and applies with `patch -p1` from the kernel tree root**, in sorted
  filename order. A new patch needs no registration; `build-image.sh` applies every `patches/*.patch`
  and refuses to continue if one fails.
- ⚠️ **`olddefconfig` drops a vendor-only symbol silently**, and a dropped driver fails at runtime, not
  at build time: no panel, dead touch. `build-image.sh` writes `dropped-symbols.txt`. Read it after any
  config change, and compare it against [§7](../SYSTEM_ANALYSIS.md#7-kernel-policy)'s table.
- ⚠️ **A kernel change is proven only by booting it**, and writing `uImage-system` is governed by the
  p1 rules in `lib/CLAUDE.md`. Take a verified backup of the *running* image first. `ssh … reboot` is
  refused by the permission classifier, so ask the operator to reboot and name the unit in the same
  sentence.
- **Reading the vendor binary beats guessing.** The vendor kernel's `/proc/kallsyms` against our
  `System.map`, compared per-function by size, names every function the vendor patched. The method is
  in [`README.md`](README.md#reading-the-vendor-kernel).
- `tools/` holds **userspace** probes built for the device (`-static`, no idiv). They are not kernel
  code, so `native_apps/check-arm-safe.sh` applies to them too.
