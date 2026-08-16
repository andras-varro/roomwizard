# lib/CLAUDE.md

The sourced-not-executed shell libraries. Loaded when you work in `lib/`.

These live at the top level rather than under `commissioning/` because the component build scripts
source `rw-bundle.sh` on the *write* side while the commissioner reads it, and all seven SSH-using
scripts source `rw-ssh.sh`. Device facts are in `SYSTEM_ANALYSIS.md`; open work in
`IMPROVEMENT_PLAN.md`; how to author the two rules files these libraries parse is in
`device-files/CLAUDE.md`.

| file | job |
|---|---|
| `rw-identify.sh` | which disk is a card, which partition holds which tree |
| `rw-clean.sh` | compile `clean-rules.conf` to a delete plan |
| `rw-provision.sh` | compile `provision-rules.conf` to an install plan, and generate the online executor |
| `rw-bundle.sh` | the release-bundle layout |
| `rw-ssh.sh` | the one answer to "can I reach this device" |
| `rw-usbpower.sh` | the only legitimate writer of `uImage-system` on p1 |

## One SSH gate, and BatchMode stays on it

**`rw-ssh.sh` is the only implementation of "can I reach this device".** Eight scripts source it —
`commissioning/provision.sh`, `commissioning/card-prep.sh`, `deploy-all.sh`, `roomwizard.sh` and all
four `*/build-and-deploy.sh` — and nine call sites go through `rw_ssh_gate`. They each keep their own
gate *call*, because a component script must run standalone; what they must not keep is their own
probe. There used to be eight, already drifted into three wordings of one message.

- ⚠️ **`BatchMode=yes` stays on the probe.** Only the *probes* set it; the `ssh`/`scp` calls behind
  them do not. So dropping it "works" and then prompts for a password **once per call** — and
  `native_apps/build-and-deploy.sh` makes dozens. The goal is never a password-driven deploy; it is
  to get a key installed once.
- **"Down" and "up but refusing us" are different answers, and only the second has a remedy.**
  `rw_ssh_classify` decides, and it matches `Permission denied` — ⚠️ **never the parenthetical method
  list.** That list is the *server's*: a RoomWizard says `(publickey,password)` because
  `card-prep.sh` sets `PasswordAuthentication yes`, and a plain `sshd` says
  `(publickey,keyboard-interactive)`. Keying on `(publickey,password)` passes against a device and
  calls every other server "down" — which *suppresses* the offer, so nothing looks broken. An
  unrecognised error classifies as `down` on purpose.
- ⚠️ **`rw_ssh_probe` reports the state twice — printed AND in `RW_SSH_LAST_STATE`/`_LAST_STDERR` —
  and the gate must use the globals.** `state="$(rw_ssh_probe …)"` runs it in a *subshell*, so the
  stderr global is discarded and every message shows a blank line where ssh's own complaint belongs.
  That was the first version of the file; it passed every wording assertion, because they grepped for
  the surrounding text rather than for what ssh said.
- **Interactive help is `[ -t 0 ]`-gated.** `release.sh` and `deploy-all.sh` drive component scripts
  as a batch, so a blocking `read` there would hang a run nobody is watching. A non-TTY caller gets
  the diagnosis plus the exact commands.
- **No stored password, and no `sshpass`.** `ssh-copy-id` asks once and stores nothing;
  `rw_ssh_keygen` makes an ed25519 key with no passphrase if the operator has none. `release.sh`
  already refuses to publish config precisely because one shipped file carries a plaintext password —
  a second one moves toward the thing that check guards. (`sshpass` *is* installed in this WSL,
  measured 2026-08-07; it is rejected on the merits, not for absence.)
- ⚠️ **A key generated under `sudo` must be chowned back.** `card-prep.sh` is called as root by
  `commission-offline.sh`, and `ssh-keygen` as root writes the key root-owned *inside the operator's
  home* — where `ssh` then needs `sudo` forever. `rw_ssh_key_owner` takes the euid as an **argument**
  so both branches are reachable from a non-root test.

## Partitions: position, never UUID

⚠️ **Never identify a partition by filesystem UUID.** A UUID is assigned at mkfs time, so it names one
*card*: units are mkfs'd independently at the factory and two RoomWizards on identical firmware share
**none** of their four UUIDs. Nothing on the device consumes one either — `root=/dev/mmcblk0p6` and
`/etc/fstab`'s `/dev/mmcblk0p{2,3,5,7}` are both by position.

`rw-identify.sh` is the one implementation: **content** for a mounted rootfs (`rw_is_rootfs`), the
**partition table** for a disk (`rw_is_card_disk`), and **position** for which partition holds which
tree (`rw_card_partitions` → `RW_PART_ROLES` = p6 root, p2 data, p3 log, p5 backup). It excludes `/`
from its scan on purpose — a content scan that selected the dev host's root would rewrite this host's
`/etc/shadow`; `rw_host_root_disk` / `rw_is_host_root_disk` are the resolved veto for the disk-level
equivalent.

⚠️ **p1 is deliberately absent from `RW_PART_ROLES`, and a test asserts its absence.** Nothing can
reach `mlo`, `u-boot.bin` or `ctrlblock.bin` through those functions — a stronger guarantee than every
caller remembering not to. p4 (extended container) and p7 (swap) are absent for the same reason:
nothing to mount. The **one** exception is `uImage-system`, reached by three deliberately-named
functions (`rw_card_boot_partition`, `rw_mount_boot`, `rw_umount_boot`) with exactly one caller,
`rw-usbpower.sh` — greppable, and a strictly smaller surface than widening the role table, which every
existing caller iterates blindly. `rw_umount_boot` must be reachable from a caller's failure trap too,
or an aborted run leaks a p1 mount.

⚠️ **A rootfs mounted offline shows `/home/root/{data,log,backup}` as three EMPTY directories** — they
are mount points for p2/p3/p5. An offline tool that mounts only p6 sees no `websign/` (the network
regenerator's input, p2), no logs (p3) and no 472 MB upgrade payload (p5), and would report success
having touched none of them. Use `rw_mount_card` / `rw_check_card_mounts`; the latter's negative half —
"a rootfs where `data` was expected means the partitions are in the wrong order" — is the half that
catches the mistake that makes every later path resolve under the wrong tree.

Measurements: `SYSTEM_ANALYSIS.md#42-partitions`. Reasoning: `COMMISSIONING.md` → *Finding the card*.

## `rw-usbpower.sh` is the only writer of p1

There is **no boot-time MD5 check** of the kernel and no signing — the only gate on `uImage-system` is
its uImage header + data CRC (which `usb_host/patch_dtb.py` recomputes correctly, and
`usb_host/verify_uimage.py` checks in pure Python — no `mkimage`, no `dtc`, neither of which is
installed here). U-Boot has no `saveenv`, so the environment cannot be persisted or corrupted.

**Rules:** never write `/dev/mtd*`; **never** overwrite `mlo`, `u-boot.bin` or `ctrlblock.bin` on p1;
stage experimental kernels under a *new* filename.

`uImage-system` has **exactly one** legitimate writer — this file, for the USB 500 mA budget — and it
is md5-gated on the way in, backed up to `uImage-system.vendor` (whose md5 is verified *before* the
original is touched), verified by re-reading the card afterwards, and rolled back on failure.

- ⚠️ **Never write a second copy of that sequence into a caller.** It is the one step
  `tests/rw_provision_test.sh` group E cannot compare between executors, so a duplicate would drift
  undetected; `tests/rw_usbpower_test.sh` group J is the stand-in comparison, running the one sequence
  over both transports.
- ⚠️ **The gate knows THREE measured md5s — vendor, 500 mA, and 500 mA + host mode — and the target is
  chosen by the CALLER**, `rw_usbpower_want`/`rw_usbpower_target_md5` off `RW_USBPOWER_WITH_MODE`.
  ⚠️ **The mode patch was applied to a unit and MEASURED NOT TO WORK** (2026-08-14), so **no caller
  can reach it**: there is no `--usb-mode` flag anywhere and all three callers `unset
  RW_USBPOWER_WITH_MODE` before driving the writer. The *library* still knows the state on purpose,
  because a unit that already carries the patch must classify as `both` and be **re-derivable back
  down**; a gate that refused it as `unknown` would leave it with no way back. It used to know two and
  `rw_usbpower_apply` returned 0 at its `patched` arm before anything ran, so on an already-commissioned
  unit a second patch wrote nothing and reported success.
- ⚠️ **Never chain one patch onto another.** `patch_dtb.py` refuses an already-patched input, so a
  transition re-derives from `uImage-system.vendor` (which step 6 proves is pristine), and that is also
  what makes the undo an ordinary power-only run — which, with no `--usb-mode` flag anywhere, is now
  the only run there is. A power-only card with no usable backup is **refused**.
- ⚠️ **Three callers now drive that one writer, and two of them do it by default**:
  `usb_host/build-and-deploy.sh`, `commissioning/provision.sh` (step 5) and
  `commissioning/commission-offline.sh` (phase 6). So **a power cycle is no longer a free undo on a
  default-commissioned unit** — `uImage-system.vendor` on p1 is the in-place remedy and a card pull the
  fallback. That is a taken decision (`IMPROVEMENT_PLAN.md` F15); do not relitigate it, and do not
  re-raise card access as a risk.
- ⚠️ **Whichever caller mounts p1 must be able to unmount it from its failure path** —
  `commission-offline.sh` carries a `BOOT_MOUNTED` variable read by `cleanup_and_exit`, ordered before
  `rw_umount_card` because `rmdir "$MOUNTED_BASE"` fails while `boot/` is still there.

Observe all of the above and JTAG never comes up. Detail and recovery procedure:
`SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery`.

## The two plan compilers

`rw-clean.sh` (delete half) and `rw-provision.sh` (install half) are the same shape: parse a
tab-separated rules file, compile it to a plan, and let each consumer keep its own **executor** —
because `/` is the correct prefix on a device and a refused one offline.

- ⚠️ **`rw_clean_del` refuses an empty or `/` base before it looks at anything else.** Unprefixed,
  those rules resolve to *this host's* `/etc`, `/opt` and `/usr/lib`. Every deletion goes through it,
  including the ones a `scope` sweep decides on.
- **Order is emitted by the compiler, not read from the file**: unlink → install → backup → link →
  touch → directive → dropline. Unlink before link (a glob would eat the link just made), install
  before link (a link to a not-yet-written file dangles on a card), dropline last (it edits files
  install may have just placed).
- ⚠️ **`dropline` uses `awk`, not `sed "/$ere/d"`.** These EREs contain slashes —
  `^4:12345:respawn:/sbin/getty 38400 tty4` closes sed's address at `respawn:` and the remainder is
  read as a command. The symptom was a passing install and an unedited `/etc/inittab`.
- **`directive` sets a key, it does not append beside it.** Substituted if present (commented or not),
  appended if absent, so it is idempotent — which matters because both bring-up paths can be re-run.
  The `sed 's/^PermitEmptyPasswords yes/…/'` it replaced matched one exact string, so
  `#PermitEmptyPasswords yes` passed through untouched and the hardening silently did nothing.
- ⚠️ **The online executor is generated, not written twice.** `rw_provision_online_script` emits a
  POSIX `sh` interpreter that `commissioning/provision.sh` pipes to the device; `install` is the one
  verb it cannot do alone, because the source bytes are on the host, so the caller `scp`s them first
  and the interpreter only sets the declared mode. Check it with `dash -n`, not `bash -n` — it runs
  under BusyBox ash.
- ⚠️ **That `scp` step is `rw_provision_push_installs`, and it reads the plan on fd 3.** `ssh` reads
  its own stdin and forwards it to the remote command, so a `while read … done < "$PLAN"` loop with an
  `ssh` in the body loses the whole rest of the plan to the *first* `ssh`: **one file installed of
  eight**, the `link` records then dangling, and the executor correctly refusing on the seven — which
  read as an executor bug. `ssh -n` fixes one body and not the next; fd 3 is a property of the loop.
  The function also **counts** — install records in versus files copied out, refusing on a mismatch —
  because the old shape was silent exactly where the copying happened. It lives here with
  `$RW_SSH`/`$RW_SCP` indirection because `commissioning/provision.sh` and
  `usb_host/build-and-deploy.sh` each had a verbatim copy of the loop, i.e. the defect twice, and
  because indirection is what makes the copy step reachable from a test with no device. **Never inline
  it again.**
- **A plan-summary line is computed, never hand-rolled.** `rw_provision_plan_summary` counts every
  record type present, including one its ordered list does not know about. The three callers' own
  arithmetic said `35 action(s) — 8 install, 9 link, 10 unlink` — 27 of 35, with backup, touch, the
  four directives and the two droplines simply missing from the breakdown.
- ⚠️ **`usb` is a provision group, and a component script compiles it through
  `rw_provision_plan_component`, not through its own `scp`/`ln -sf`.** `rw_provision_plan_component
  FILE GROUP` compiles one optional group's records for `usb_host/build-and-deploy.sh` — a separate
  entry point rather than a flag on `rw_provision_plan`, and it refuses `base`, so a commissioning
  path cannot reach a base-less plan by mistyping a group list.

## Bundles: one layout, declared modes, no configs

`release.sh` exists so that putting apps on a device does not require reproducing the toolchain
(`IMPROVEMENT_PLAN.md` F9). It calls `build-and-deploy.sh --bundle <dir>` on all four components. The
layout lives in **`rw-bundle.sh`** and nowhere else: `<dir>/root/<device-path>` plus
`<dir>/manifest.d/<component>.{list,md5}`.

- ⚠️ **Modes are *declared* by the caller, never read off disk.** `/mnt/c` reports every file 0777 and
  discards `chmod`, so `stat -c %a` here is a constant, not a measurement. `rw_bundle_add` takes the
  mode as an argument and `.list` is the authority.
- **`rw_bundle_check` asserts both directions** — no manifest entry without a staged file, *and* no
  staged file without an entry. The second is the one that catches a file added by hand that nothing
  will ever `chmod`.
- **`release.sh` greps the staged manifest and refuses to publish config** (`*.conf`, `/etc/hosts`,
  `/etc/hostname`, `rw_config`, `touch_calibration`, `input_config`). Not a rule each component is
  trusted to remember — the negative control for the one that forgets. Device config carries the
  `/etc/hosts` mapping that names the device's own host at an unreachable external address, and
  `vnc_client`'s plaintext VNC password.
- ⚠️ **And it refuses to publish vendor firmware** — any entry whose basename is `uImage*`, `mlo`,
  `u-boot*` or `ctrlblock*`. Matched on the basename, not a path, because p1 is not a bundle path at
  all: a staged copy would arrive at some invented location. `uImage-system` is a 5.2 MB Steelcase
  binary and this repo is meant to be published, which is why the USB 500 mA patch is **derived** from
  the device's own copy rather than shipped. `usb_host`'s `--bundle` carries the four *built*
  artifacts only.
- ⚠️ **A new staged file is a licence decision, and `LICENSE.md` is where it is recorded.** Ask whether
  the file is *ours*: `scummremastered.zip`, `gui-icons.dat` and `vkeybd_roomwizard.zip` are all
  GPL-3.0+ ScummVM data and were being published with no licence line until someone looked, and
  `vkeybd_roomwizard.zip` is the **only** non-MIT file committed in this repo. `LICENSE.md` is the
  repo-level half of `release.sh`'s per-release `NOTICE`; **the two must agree**, and MIT governs our
  *source* — it does not decide the licence of a binary it links into (`scummvm` is GPL-3.0+ as a
  whole, `vnc_client` GPL-2.0+). Measure a dependency's licence *version* rather than carrying it
  forward: `NOTICE` claimed GPLv2+ for ScummVM and the tree's `COPYING` is GPLv3.
- **`--stage-only` is the tested path; `--tag` has never run.** `gh` 2.86.0 is installed in WSL (from
  the release `.deb` — focal's apt has no `gh`, and the snap links against a glibc newer than 2.31), so
  the publish step is reachable but still unexercised. The tarball `--stage-only` produces is a
  first-class input to the offline installer, so everything downstream is testable with no network.

## Regressions

Host-only, no device, no root: `tests/rw_ssh_test.sh`, `tests/rw_provision_test.sh`,
`tests/rw_clean_test.sh`, `tests/rw_identify_test.sh`, `tests/rw_usbpower_test.sh`, plus the
`tests/measure_*_sabotage.sh` harnesses that re-measure them. What each one can and cannot see, and
the traps in extending them, are in `tests/CLAUDE.md`.
