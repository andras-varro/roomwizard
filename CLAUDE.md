# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

Projects that repurpose the **Steelcase RoomWizard** — a wall-mounted meeting-room display — into a
games/apps platform. The device is an embedded Linux box: **TI OMAP3503** ARM Cortex-A8 @ 600 MHz,
234 MB RAM, 800×480 framebuffer, projected-capacitive touch, kernel 4.14.52, SysVinit. No GPU, no
DSP — all rendering is software. Display is legacy **omapfb/omapdss**; there is no DRM/KMS.

**There is no local app to run.** Everything is cross-compiled on the dev host and deployed over SSH
to a physical device (reference unit `192.168.50.73`, aka RW09). Verifying a change means deploying it
and looking at the panel — framebuffer screenshots via `fb565_to_png.py`.

### One fact, one home

Three top-level docs with disjoint jobs. **If something appears in two of them, the other copy is
stale.**

| Doc | Answers | Read it |
|---|---|---|
| `SYSTEM_ANALYSIS.md` | *What is true about this device?* — silicon, board, boot chain, OS, traps | Before touching anything hardware-related (audio, USB, watchdog, boot, GPIO, cross-compile) |
| `IMPROVEMENT_PLAN.md` | *What should we do about it?* — open bug + feature backlog with `file:line` | **Before starting work**, so you don't rediscover a known bug |
| `CLAUDE.md` (this file) | *What must I know before my first edit?* | Loaded every session |

`SYSTEM_ANALYSIS.md` is organised by subsystem — what's there, how to drive it, the gotchas — plus the
full board teardown and a photo index for [`HardwarePhotos/`](HardwarePhotos/).
`IMPROVEMENT_PLAN.md` holds **open work only**; closed items are in `git log --grep=<id>`.

Each component directory has its own `CLAUDE.md` (`native_apps/`, `scummvm-roomwizard/`,
`vnc_client/`), loaded automatically when you work on files there. **Those describe their own code
only** — device facts belong in `SYSTEM_ANALYSIS.md`, open work in `IMPROVEMENT_PLAN.md`. Link, don't
copy. `native_apps/CLAUDE.md` is the deepest of the three; go there for the rendering loop, the input
model, the touch model and the screen rectangles.

**Kernel policy: do not attempt a kernel rebuild or upgrade.** The vendor's kernel source is not
available (the repo's `usb_host/linux-4.14.52/` is vanilla upstream, missing both `panjit_ts` and the
panel driver), and requesting it from Steelcase is ruled out. Anything gated on a kernel config change
is out of scope — `SYSTEM_ANALYSIS.md#7-kernel-policy`.

## Working style

**Delegate the reading.** The docs are long by design and the sources are large (`device_tools.c` is
2651 lines). Answering "where is X / which call sites do Y / does this pattern hold across all seven
games" by reading files into the main context burns the budget the actual edit and its verification
need. Spin up subagents for that — one per independent question, in parallel — and keep only their
conclusions. Do the edits, the build and the on-device verification in the main thread.

**A recorded cause and a prescribed fix are both hypotheses.** `open, confirmed` in
`IMPROVEMENT_PLAN.md` means the *symptom* reproduced — nothing more. The cause written beside it and
the fix suggested under it were both written by someone who had not yet done the work, and both have
been wrong more than once, each time refutable by a single `grep`. So: reproduce the symptom, then
find the cause yourself. **When an entry says "compare with X", read X** — do not assume it contains
the pattern being described. And before setting a flag, read every use of it: the first mechanism that
*would* produce the symptom is not automatically the right fix.

**Give every new check a negative control, and ask which part of the count is the harness.** A gate
that reports a number can be wrong in both directions, and this repo has hit both — a gate counting
artifacts it could not actually read (false negative), a `/proc` scanner counting its own `grep` argv
(false positive), and the `sdiv`/`udiv` gate firing ~9 times on a *stripped* binary and zero on the
identical file unstripped. Put search patterns and multi-line harnesses in files, not in argv. Skip
inputs your tool cannot inspect rather than passing them. **If a fix is supposed to drive a number to
zero, check that it reaches zero** — a small residue is the tell, both times it happened.

**Write the failing version first.** There is no CI, so a test that has only ever been seen passing is
not evidence that it can fail. Compile it against the pre-fix source and count the failures.

**Say what you could not verify.** "Latent, not reproducible from this host" and "reported as *I think
it works*" are the honest forms. Do not promote a hedge to a confirmation.

**Editing the long docs.** Anchor an `Edit` on text you intend to **keep**, and expect to fix inbound
links whenever you retitle a heading. The IDE's markdownlint is a free dangling-link checker —
**`MD051/link-fragments` fires on exactly the anchors you just broke**; `comm -23` of the
`(#fragment)`s against the slugified headings confirms a whole file. (`MD060` table warnings fire on
every table in the repo — that is the established style, not something you introduced.) A long
multi-line `old_string` can fail to match for no visible reason; fall back to
`head -N` / `cat <<'EOF'` / `tail -n +M`, which also preserves LF. **Never bulk-edit a source file
with a python script** — it rewrites the whole file's line endings. Use `Edit`, or `sed -i`.

## Build & deploy

Everything builds with the ARM cross-compiler and deploys over SSH. There is **no CI, no test runner,
no lint** — "tests" are host-gcc regressions over pure-logic functions (`native_apps/tests/*_test.c`)
plus shell regressions over host tooling (`tests/*_test.sh`, run directly) plus interactive on-device
diagnostic tools.

```bash
./roomwizard.sh                     # front door: a menu over everything below

./commissioning/card-prep.sh        # bring-up 1: SD-card prep (offline; sets host name)
./commissioning/provision.sh <ip>   # bring-up 2: SSH cleanup + init service + SW-watchdog bypass

sudo ./commissioning/commission-offline.sh --bundle <tar.gz|dir>   # all of the above, offline, ONE boot

./deploy-all.sh <ip>                # build + deploy everything (native_apps first)
./deploy-all.sh <ip> <component>    # one component;  --list  to see them
./deploy-all.sh --from-bundle <b> <ip>   # install a release bundle; build NOTHING

cd native_apps && ./build-and-deploy.sh [<ip>] [set-default]

./release.sh --stage-only           # build all components + stage one offline bundle + tar
./release.sh --tag <tag>            # the above, then `gh release create`  (gh 2.86.0 in WSL; never run)
```

**Three directories, three jobs.** `lib/` holds the sourced-not-executed libraries (`rw-identify.sh`,
`rw-clean.sh`, `rw-bundle.sh`, `rw-provision.sh`) — at the top level, not under `commissioning/`,
because the component build scripts source `rw-bundle.sh` on the *write* side while the commissioner
reads it. `commissioning/` holds the bring-up scripts, none of which is an answer to "what do I run"
except through `roomwizard.sh`. `device-files/` holds what is installed onto a device verbatim. The
three scripts at the root are the three front doors: `roomwizard.sh`, `deploy-all.sh`, `release.sh`.

`--help` on any of them is current; `README.md` has the annotated walkthrough. Two shapes worth
knowing without looking them up: **`set-default` is the only mode `native_apps/build-and-deploy.sh`
accepts** (anything else is rejected rather than ignored), and **cleanup, bloatware removal and the
boot service all live in `commissioning/provision.sh`** — `--remove`, `--deep-clean`, `--status`, `--hostname
NAME` — never in a component script. **Both `--remove` and `--deep-clean` read their decisions from
`device-files/clean-rules.conf`**, the same file `commissioning/commission-offline.sh` uses, and differ from each
other only by that file's `sweeps` group.

### Bundles: one layout, declared modes, no configs

`release.sh` exists so that putting apps on a device does not require reproducing the toolchain
(`IMPROVEMENT_PLAN.md` F9). It calls `build-and-deploy.sh --bundle <dir>` on `native_apps`,
`vnc_client` and `scummvm-roomwizard` — **never `usb_host`**, which patches `uImage-system` on p1. The
layout lives in **`lib/rw-bundle.sh`** and nowhere else: `<dir>/root/<device-path>` plus
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
  `/etc/hosts` mapping of D7 and `vnc_client`'s plaintext VNC password.
- **`--stage-only` is the tested path; `--tag` has never run.** `gh` 2.86.0 is now installed in WSL
  (from the release `.deb` — focal's apt has no `gh`, and the snap links against a glibc newer than
  2.31), so the publish step is reachable but still unexercised. The tarball `--stage-only` produces is
  a first-class input to the offline installer, so everything downstream is testable with no network.

### `.app` manifests: one generator per component, on disk

The nine `native_apps` manifests are **data in `native_apps/app-manifests.sh`**; `vnc_client` and
`scummvm-roomwizard` each write theirs from **one** heredoc into a file. They used to be `cat > … << APP`
blocks inside an `ssh "$DEVICE" bash <<'REMOTE'` heredoc, i.e. they existed only when a device was
reachable — so the offline installer could not produce the same bytes without a second copy, and an
`exec=` path drifting between the two renders a launcher tile that does nothing when tapped. **Write the
manifest to a file, then copy it**; never emit one from inside an `ssh` heredoc again.

### `device-files/` and the two rules files: one copy, two consumers

Anything installed onto a device **verbatim** by more than one path lives in `device-files/`, never in a
heredoc: `roomwizard-app` (the boot init script, which is why it carries the name it is *deployed* as
rather than a `.sh` one), `disable-steelcase.sh`, `audio-enable`, `time-sync`, `99-security.conf`, plus
the two data files `clean-rules.conf` and
`provision-rules.conf`. Both `commissioning/provision.sh` (over SSH) and `commissioning/commission-offline.sh` (onto a mounted
card) install those same bytes, and neither decides *what* to install or delete — both read the rules.
**Every `install` record in `provision-rules.conf` has a `device-files/` source**, which is the check
that a new one is in the right place.
`.gitattributes` pins `device-files/**` to `eol=lf`, because the init scripts have no `.sh` extension —
`/etc/init.d/audio-enable` is the name the `rc5.d` link points at — and a CRLF shebang is rejected by
BusyBox as a misleading "no such file or directory".

**Two data files, one shape.** `clean-rules.conf` (4 fields) says what is *removed*;
`provision-rules.conf` (6 fields) says what the device ends up *with*. Both make the reason mandatory
and last, both are compiled to a plan by a library, and both are executed twice — once with `/` over
SSH, once under `$BASE/root` offline. `lib/rw-provision.sh` is the install half of what `lib/rw-clean.sh` is for
the delete half.

- **`provision-rules.conf` is `<type> <group> <mode> <target> <source> <reason>`**, types `install` /
  `link` / `link-opt` / `unlink` / `touch` / `backup` / `directive` / `dropline`. Every column means one
  thing for every type; `-` is the explicit not-applicable and an *empty* field is an error.
- ⚠️ **A `link` source must be RELATIVE** — `../init.d/time-sync`, never `/etc/init.d/time-sync`. An
  absolute symlink target is correct on a running device and *dangling on a mounted card*, and a
  dangling `rc5.d` link is skipped in silence at boot. It is the one defect this file could introduce
  that nothing downstream would catch, so validation rejects it.
- ⚠️ **`rw_provision_check_keeps` asserts every boot link is on `clean-rules.conf`'s keep list.** A link
  installed but not whitelisted is deleted by the next `--deep-clean`, so the unit boots right once and
  loses it. That pairing used to be a comment in *both* files asking a human to remember.
- **Order is emitted by the compiler, not read from the file**: unlink → install → backup → link →
  touch → directive → dropline. Unlink before link (a glob would eat the link just made), install before
  link (a link to a not-yet-written file dangles on a card), dropline last (it edits files install may
  have just placed).
- ⚠️ **`dropline` uses `awk`, not `sed "/$ere/d"`.** These EREs contain slashes —
  `^4:12345:respawn:/sbin/getty 38400 tty4` closes sed's address at `respawn:` and the remainder is read
  as a command. The symptom was a passing install and an unedited `/etc/inittab`.
- **`directive` sets a key, it does not append beside it.** Substituted if present (commented or not),
  appended if absent, so it is idempotent — which matters because both bring-up paths can be re-run. The
  `sed 's/^PermitEmptyPasswords yes/…/'` it replaced matched one exact string, so `#PermitEmptyPasswords
  yes` passed through untouched and the hardening silently did nothing.
- ⚠️ **The online executor is generated, not written twice.** `rw_provision_online_script` emits a POSIX
  `sh` interpreter that `commissioning/provision.sh` pipes to the device; `install` is the one verb it cannot do
  alone, because the source bytes are on the host, so the caller `scp`s them first and the interpreter
  only sets the declared mode. Check it with `dash -n`, not `bash -n` — it runs under BusyBox ash.
- **`--keep-<group>` switches off part of the clean; `--no-<group>` part of the provision.** Each is
  named after the groups in its own data file, so neither list is repeated in a script.
- Regression: `tests/rw_provision_test.sh` (94 cases, host-only, no card, no root). Its group E is the
  one that matters: **both executors' `--dry-run` over the same plan must print the same resolved set**,
  compared through `rw_provision_canonical`, which strips only the resolved host path. That is the sole
  check that catches the drift this file exists to remove, and the online half is exercised by running
  the *generated* interpreter against a copied tree — a comparison of two executors rather than of one
  executor and a wish.

**`device-files/clean-rules.conf` is the one place a keep or a delete is decided.** Four tab-separated
fields, `<type> <group> <path> <reason>`, record types `scope` / `keep` / `delete` / `truncate`; a line
with fewer than four fields is an error, because a missing reason is how a delete gets in without anyone
having to justify it. `lib/rw-clean.sh` parses it and compiles it into a plan; each consumer keeps its own
**executor**, because `/` is the correct prefix on a device and a refused one offline.

- ⚠️ **`rw_clean_del` refuses an empty or `/` base before it looks at anything else.** Unprefixed, those
  rules resolve to *this host's* `/etc`, `/opt` and `/usr/lib`. Every deletion goes through it,
  including the ones a `scope` sweep decides on.
- **`scope` is a whitelist sweep**, which is what makes an unrecognised vendor service on a unit nobody
  has inspected removed *by construction*. It never recurses, so a kept directory's contents are never
  examined.
- **`--remove` and `--deep-clean` are one mechanism differing by one group.** All nine `scope` records
  are in the `sweeps` group, so `--remove` is exactly `--deep-clean --keep-sweeps`: the named vendor
  stacks go, and a path no `keep` names stays. `--remove` used to be ~85 lines of hardcoded `rm -rf`
  inside an `ssh <<'REMOTE'` heredoc.
- ⚠️ **A rule the sweeps would cover anyway may still need to be named.** The eight vendor logs under
  `/home/root/log` are named explicitly *and* swept, because the sweep is in `sweeps` and `--remove`
  runs without it. Dropping them looks safe against `--deep-clean`'s plan and loses them from
  `--remove`'s — which is why a fold must be checked against **both** plans, not the default one.
- ⚠️ **The whole vendor stack goes by default, factory-restore payload included.** `--keep-factory` is
  the only opt-out, and the gate is the full-card-backup question every bring-up path asks first. The
  reasoning is in the rules file: the payload restores software whose start-up this same clean removes,
  so keeping it preserves only the ability to undo a commissioning it can no longer perform. Do not
  reintroduce a middle setting.
- ⚠️ **A DISABLED group's paths are protected from every sweep**, not merely skipped by their own
  `delete` line — otherwise `--keep-java` leaves `/opt/openjre-8` named by a delete nobody runs and the
  `/opt` sweep removes it anyway. What `--keep-<group>` does *not* do is re-enable a boot link. Note the
  `keep` records deliberately stay in group `base`: a disabled group's keeps are *dropped*, so filing
  them under `sweeps` would silently unprotect all sixty.
- ⚠️ **`rw_clean_validate` rejects a rules file that names `rc0.d` or `rc6.d`.** They are shutdown, not
  startup — `umountfs`, `sendsigs`, `save-rtc.sh` — so they are unreachable by construction, the same
  guarantee as p1's absence from `RW_PART_ROLES`.
- **A glob is allowed only in the last path component.** `rw_clean_del` quotes the directory part so a
  base containing a space still resolves, which means a mid-path glob would be taken literally and the
  rule would silently match nothing. Validation refuses it.
- Regression: `tests/rw_clean_test.sh` (148 cases, host-only, no card, no root). The fixture is
  **synthetic, with real symlinks** — `partitions/` and `partitions.new/` cannot be it, see *Working
  from this host*. `tests/measure_sabotage.sh` re-measures it against six deliberately broken copies
  and prints the counts its header claims; the sabotages are in a file because a `sed` pattern with
  `\t` in it does not survive `wsl.exe -e bash -lc` quoting, and one that fails to apply reports
  "0 failed" — indistinguishable from a suite that cannot detect the breakage.

### Offline commissioning verifies what it installed, on the card

`commissioning/commission-offline.sh` md5s every **installed** file against the bundle manifest, asserts `+x` (real
ext4 honours it, so that check is a measurement offline and cannot be one on `/mnt/c`), runs
`check-arm-safe.sh` over the **downloaded** binaries, asserts every `.app`'s `exec=`/`icon=` and that
`default-app` names one of them, and `dash -n`s every `/bin/sh` script it wrote.

- ⚠️ **`dash -n` catches parse errors and CRLF, not bashisms.** `[[ -n "$x" ]]` parses fine under dash —
  `[[` is read as a command name — so it passes and then fails at boot with `[[: not found`.
- ⚠️ **A missing `arm-linux-gnueabihf-objdump` is a refusal, not a pass.** `check-arm-safe.sh` skips
  non-ARM files and then reports "no hardware divide in 0 binaries", so the caller counts the ELF
  candidates itself and says loudly what it did not check. `--arm-check=skip` is the deliberate override.
- ⚠️ **`commissioning/card-prep.sh` is a *step of* the offline pass, not an alternative to it**, and the
  handover carries **two** variables. `ROOTFS` skips its own card detection; `RW_COMMISSION_ORCHESTRATED`
  suppresses its closing banner and the `NEXT_STEPS` block it reads out of `COMMISSIONING.md`, which
  tells the operator to run `commissioning/provision.sh` and `deploy-all.sh` — both already done by the time it
  prints. They are separate on purpose: **`ROOTFS` alone also means "I mounted the card myself"**, and
  that operator does still need the next steps. Suppressing on `ROOTFS` is the obvious wrong fix and
  passes every other case.
- Regression: `tests/commission_offline_test.sh` (21 cases; needs root and a staged bundle). Every check
  has a sabotage case; the fixture builder is `tests/make-fake-card.sh`, which must live under `/tmp`
  because DrvFs cannot hold a symlink.
- Regression: `tests/commission_prep_test.sh` (17 cases, host-only, no card, no root) covers
  `commissioning/card-prep.sh`'s two host-side decisions — whose `~/.ssh` the key is looked for in (under
  `sudo`, `$HOME` is `/root`), and the suppression above. Neither is reachable by running the script, so
  both are **extracted from the shipped file by line range** and run against stubs. Measured against
  deliberately broken copies: the `ROOTFS`-sniffing fix fails 4, a call site that ignores
  `operator_home()` fails 3, an orchestrator that forgets the flag fails 1. ⚠️ **An earlier version of
  that harness re-emitted the `OPERATOR_HOME=` assignment itself and thereby repaired the sabotage it
  was meant to catch** — extract the wiring, never restate it.

Components (each a subdir with a `build-and-deploy.sh`): `native_apps` (C games + launcher + tools),
`scummvm-roomwizard` (ScummVM backend port), `vnc_client`, `usb_host` (USB host-mode enablement +
Xbox controller modules).

**Toolchain:** `arm-linux-gnueabihf-gcc` (`sudo apt install gcc-arm-linux-gnueabihf`). ScummVM
additionally needs **WSL Ubuntu 20.04+** and `g++-arm-linux-gnueabihf`; it cross-compiles its own
zlib/libpng. `usb_host` needs kernel-module build deps (`bc libssl-dev bison flex`) + `python3`.

**`native_apps/` has no `Makefile`.** `native_apps/build-and-deploy.sh` (cross-compiler, `-static`) is
the only build path and the source of truth for how binaries are built and shipped. A **new** binary
goes in `GAMES_BINARIES` there and nowhere else — that one array drives the upload, the remote
`chmod +x` and the md5 verification. Details: `native_apps/CLAUDE.md`.

### Redeploy scope by changed file

All three components link objects from `native_apps/common/`, so what you touched decides how much has
to go out. A *deployed* binary keeps whatever it was built with, and a stale one does not error — it
misparses.

| Changed | Redeploy |
|---|---|
| an app's own source, `common/common.c`, `common/gamepad.c` | `native_apps` |
| `common/hardware.c`, `common/config.c`, `common/logger.c` | `native_apps` + `vnc_client` |
| `common/framebuffer.c`, `common/touch_input.c` | **all three** — `./deploy-all.sh <ip>`; ScummVM is the slow one |
| anything in `device-files/` (`roomwizard-app`, `disable-steelcase.sh`, the rules files, …) | neither — **only** `./commissioning/provision.sh <ip>`, which ends in a reboot (or `commissioning/commission-offline.sh`, offline) |

When in doubt, over-deploy. The failure mode is silent.

## Working from this host — Windows, WSL and the tools

The repo lives on `c:\work\roomwizard`; **everything that compiles or decodes lives in WSL.** These are
tool-level traps rather than device facts, and each has cost real time.

- **WSL is not just for the cross-compiler.** Git Bash has no host `gcc` and no usable `python3`, so
  the host regressions (`tests/*_test.c`) and every `fb565_to_png.py` decode need WSL too. Invoke as
  `wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard/<component> && ./build-and-deploy.sh <ip>"`.
- ⚠️ **`command -v python3` succeeds in Git Bash and the interpreter does not exist.** It resolves to
  the Windows App Execution Alias — a real file that prints *"Python was not found"* and fails. Test by
  running `python3 --version`, not by looking it up. WSL's python3 has `PIL`, which the framebuffer
  decoder needs.
- ⚠️ **Never measure a prerequisite from Git Bash — the answer is "absent" for everything.** `gcc`,
  `arm-linux-gnueabihf-*`, `sfdisk` and `gh` are all present in WSL and all absent there, so a
  `command -v` sweep run in the wrong shell reports a host with no toolchain at all. That happened
  (2026-08-06): it was recorded as fact, made `IMPROVEMENT_PLAN.md` F11 read as a hard blocker on all
  building, and attributed a test failure to a missing `sfdisk` that is installed. **State which shell a
  prerequisite claim was measured in**, and measure with `wsl.exe -e bash -lc`. The measured inventory
  lives in `IMPROVEMENT_PLAN.md` F11.
- **Git Bash `/tmp` and WSL `/tmp` are different filesystems.** Write captures somewhere under
  `c:\work\roomwizard` and both sides reach them. ⚠️ **WSL's `/tmp` also does not survive between
  `wsl.exe` calls** — the instance idles out and takes it with it, so a fixture staged in one call can be
  gone by the next. Stage and use it inside **one** `wsl.exe -e bash -lc`, or put it under the repo.
- **The Bash tool's working directory does not reliably persist between calls.** Use absolute paths.
- **No foreground `sleep`** — it is blocked. Use `run_in_background`, or put the sleep inside the remote
  command: `ssh root@<ip> 'sleep 3; …'` works, because the local command is `ssh`.
- **A compound `ssh` command can be refused by the permission classifier.** Re-issue it as one plain
  single-purpose command. The same applies to a `bash -c` heredoc whose body contains apostrophes.
- **File modes are unobservable here.** `/mnt/c` is DrvFs 9p: it reports every file `-rwxrwxrwx` and
  silently discards `chmod`. A missing-`+x` bug can neither fire nor be demonstrated on this host, and
  you cannot build a negative control for one by `chmod`ing under `/mnt/c`.
- ⚠️ **The card captures under `partitions/` and `partitions.new/` contain no symlinks at all.** They
  were copied through Windows, which dropped every one: `bin/sh` and `bin/busybox` are simply absent,
  and `etc/rc0.d` … `etc/rc6.d`, `etc/rcS.d` are all **empty directories**. So the captures are a fine
  source for regular files — vendor scripts, `/etc` config, sizes — and **worthless for any question
  about what starts at boot or where a symlink points**. Get that from a running unit
  (`SYSTEM_ANALYSIS.md#52-as-we-run-it--game-mode` has the measured inventory), and build fixtures for
  boot-link work synthetically rather than from a capture.
- **Every disk here reports `removable = 0`**, including the root disk, and a `wsl --mount`ed card
  lands on the same `/dev/sd?` names as `/`. So a "removable media only" gate rejects everything.
  Resolve the root disk before writing to any device:
  `lsblk -rnso NAME "$(findmnt -no SOURCE --target /)" | tail -1`. `mount | grep ^/dev/sdX` is not a
  substitute — it never lists swap.
- **`shellcheck` is not installed in this WSL** (`IMPROVEMENT_PLAN.md` C7). `bash -n` is what you have,
  plus `dash -n` on anything carrying a `/bin/sh` shebang.
- **Never run a ScummVM build concurrently with a `native_apps` build.**
  `scummvm-roomwizard/build-and-deploy.sh` does `rm -f native_apps/common/*.o` at two points —
  deliberately, because a stale x86 `.o` there fails the cross-build with "file format not
  recognized". It takes ~1m35s–2m20s; wait it out.
- Markdown files are **LF** in the working tree but `.gitattributes` pins only `*.sh`, `*.py`, `*.conf`
  and `*.app`. With `core.autocrlf=true` set on this host, expect git to warn about LF→CRLF on docs.

## Non-obvious constraints (things that will silently break)

- **You cannot script a touch interaction, and the tool that claims to will lie to you.** There is no
  `/dev/uinput` (`CONFIG_INPUT_UINPUT` unset, no module), and evdev's `write()` path is for *output*
  events (force feedback, LEDs) — so `tests/touch_inject.c` writes to `/dev/input/event0`, **prints
  "injected successfully", exits 0, and delivers nothing to any reader.** Automated on-device
  verification therefore stops at the **first** screen: SSH-launch the binary, `cat /dev/fb0`, decode,
  assert. Past that, write a tap-by-tap checklist for a human instead of a test.
  ⚠️ **This is a limit on the *device*, not on the code.** `gamepad.c`'s whole state machine is
  host-testable — `gamepad_poll()` takes the touch coordinate as a plain argument and its evdev sources
  are `read(2)` on an fd, so a temp file of `struct input_event` drives the real path. Ask whether the
  thing needs the *kernel* or only needs *events*. Detail: `native_apps/CLAUDE.md` → *Input*.
- **Cortex-A8 has no hardware integer divide.** A binary containing an `sdiv`/`udiv` *instruction*
  crashes instantly with SIGILL (exit 132) — blank screen, no output, no log, indistinguishable from
  "the app didn't start". **Verify with `native_apps/check-arm-safe.sh`**, which now runs from **all
  three** component build scripts — from `native_apps/build-and-deploy.sh` before every deploy, before
  every `--bundle` and on build-only runs; from `vnc_client`'s after `make`; and from ScummVM's
  **inside `strip_binary`, before the in-place `strip`**, because that is the only moment an unstripped
  ScummVM binary exists. The expected count is a **hard zero** across all 31 native ARM artifacts, and
  it is zero. The bare `$CC -O2 -static` deploy path is already safe; what would break it is an
  explicit `-march` implying the idiv extension. Two ways to get a wrong answer out of the gate —
  matching the line instead of the mnemonic field, and gating a *stripped* binary — are in
  `SYSTEM_ANALYSIS.md#61-cortex-a8-has-no-hardware-integer-divide`.
- **Framebuffer bpp is per-app — confirm it before decoding a screenshot.** `/dev/fb0`'s format is
  global mutable state: every native app pins **32bpp XRGB8888** via `fb_set_bpp(dev, 32)` before
  `fb_init()`; **ScummVM and the VNC remote session run 16bpp RGB565** to halve write bandwidth.
  `framebuffer.c`'s primitives dispatch on `bytes_per_pixel`, so the public API takes RGB888 at either
  depth and no depth corrupts the buffer — the reason to pin is **determinism and appearance**, not
  memory safety.
  Screenshot: `ssh root@<ip> cat /dev/fb0 > fb.raw`, then `python3 fb565_to_png.py fb.raw fb.png`
  (defaults to 32bpp; `--bpp 16` for ScummVM/VNC screens). **Run `fbset | grep geometry` on the device
  and believe it** — an app killed mid-session leaves the panel in a mode nothing running asked for,
  and a 32bpp frame is exactly the size of two 16bpp pages, so file size never catches the mistake.
  Full table and the RGB565-read-as-32bpp signature: `SYSTEM_ANALYSIS.md#32-display`.
- **Screen edges are the library's problem, not the app's.** The bezel hides ~15 px top and bottom.
  `fb_init()` shrinks the drawing surface to the visible rectangle and `fb_swap()` places it at the
  viewport origin, leaving the hidden bands black — so `fb.width`/`fb.height` and `SCREEN_VISIBLE_*`
  are the **logical** screen (800×450 at the shipped margins) and every pixel in it is visible. Never
  add your own bezel arithmetic.
- **Drawable ≠ pressable.** The digitizer's reading saturates *inside* the panel edge — a band of
  ~30 px at each end of Y, much smaller on X but not always zero. So there are **two** rectangles:
  `SCREEN_VISIBLE_*` (everything drawable — backgrounds, titles, status rows, playfields) and
  `SCREEN_SAFE_*` (visible **∩** touchable — buttons, toggles, tab bars, touch grids). The band
  between them is good screen area and stays fully drawable; it just must not hold anything the user
  has to press. **A dead band is a fact about this panel, not a bug.**
  The inset is **measured at runtime, never hardcoded or asserted** — it is per-unit and
  per-calibration-run, and `0` until a panel has been swept. Read it from Device Tools → Display →
  `TOUCHABLE:`. Choosing per call site needs the **hit-test**, not the look: see
  `native_apps/CLAUDE.md` → *Screen edges*. Numbers and method: `SYSTEM_ANALYSIS.md#33-touch`.
  **ScummVM and `vnc_client` are the exception to "the band is drawable, so use it":** their content is
  third-party, so nobody can audit which of its pixels must be pressable. Both confine the *content
  rectangle itself* to `SCREEN_SAFE_*`, with a per-component opt-out that moves only the picture, never
  their own buttons.
- **Touch coordinates must be captured before the `BTN_TOUCH` press event** (order: ABS_X, ABS_Y,
  BTN_TOUCH, SYN_REPORT). `common/touch_input.c` maps a reading in **two stages** — raw 12-bit →
  **panel** pixel by a per-axis piecewise-linear curve, then panel → **logical** by subtracting the
  bezel viewport origin. Stage 1 is calibration, stage 2 is the bezel; they are separate, and a fit for
  stage 1 must be done in panel coordinates or the bezel is subtracted twice.
  - ⚠️ **Never clamp a fitted endpoint into `0..4095`.** A correct fit legitimately extrapolates
    outside it, because the interior line reaches the raw limits *before* the panel edge. Endpoints
    outside the emittable range are the measurement, not an error.
  - **The fit lives in `common/touch_calib.c` and nowhere else** — it previously existed in three
    places with the same defect. Both geometry lines are written by one wizard: Device Tools →
    Display → `CALIBRATE TOUCH`.
  - ⚠️ **ScummVM and `vnc_client` each link their own `touch_input.o`, so redeploy every component
    after changing that file or the config format.** A stale binary does not error: its 4-number
    `sscanf` succeeds on the first four of the eight values, accepts them as a legacy config, and touch
    silently collapses. Extend the config with new keyword-tagged trailing lines; **never change line
    1's field count.**
  - Format, the measured reach, the model and the reference capture: `SYSTEM_ANALYSIS.md#33-touch`.
    Authoring rules: `native_apps/CLAUDE.md` → *Touch model*.
- **Don't probe I2C bus 1.** `pv02_app 5` (the vendor light-sensor factory test) can hang the bus, and
  **bus 1 carries the PMIC** — `SYSTEM_ANALYSIS.md#39-i2c`. There is no light sensor to find: the
  enclosure has no aperture at all.
- **A stock unit may be unreachable, and the vendor rewrites the network files on every boot.**
  `/opt/sbin/networkmanager` regenerates `/etc/hostname`, `/etc/hosts`, `/etc/resolv.conf` and
  `/etc/dhclient.conf`'s `send host-name` from `/home/root/data/websign/net.*` at each boot, so an
  offline edit of `/etc/hostname` alone is undone (`IMPROVEMENT_PLAN.md` D7b). **If
  `websign/net.mode` is `manual` the unit takes a static address and sends no DHCP request** — it
  appears in no router lease list and SSH is impossible until the card is edited offline. The deep
  clean deletes `websign/`, which is what makes a name stick on a *cleaned* unit. The vendor's own
  validator **rejects hyphens**, so prefer `rwtest` to `rw-test` on anything still carrying the vendor
  stack. ⚠️ **`/home/root/{data,log,backup}` are mount points (p2/p3/p5)** — a rootfs mounted offline
  shows all three empty, and the vendor's config and logs are on those other partitions. Detail:
  `SYSTEM_ANALYSIS.md#35-network-and-power`.
- **The Steelcase software watchdog reboots the device ~every 70 min** in game mode. It is a cron job
  (`/opt/sbin/watchdog/watchdog.sh`), and **`disable-steelcase.sh` is what disables it** —
  `touch /var/watchdog_test` as its *first* command, deliberately ahead of every fallible line, plus a
  freshly written crontab. It prints whether the bypass is in place. `commissioning/provision.sh <ip>` deploys and
  runs it; `/etc/init.d/roomwizard-app` re-runs it on **every boot**.
  **A device can be running an older copy than the repo's until `commissioning/provision.sh` is re-run** —
  `./commissioning/provision.sh <ip> --status` md5s both deployed scripts against the repo's and says
  `matches repo` or `DRIFTED` per file (read-only, no reboot). **Check that before reproducing anything
  against a device**, or you will draw conclusions about code the device is not running.
  The *hardware* watchdog (`/usr/sbin/watchdog`) is fine — keep it, but any app that takes over the
  screen for long periods must keep feeding `/dev/watchdog` (60 s) or the device hard-resets.
- **Audio via OSS `/dev/dsp` is buggy** (ALSA OSS shim on TWL4030): open with `O_NONBLOCK` and handle
  `EAGAIN` or you get a "bru-bru-KLICK" stall (~506 ms ALSA HW period); apply ~50 % software
  attenuation (the small speaker distorts at full scale); `SNDCTL_DSP_STEREO` is silently ignored, and
  SPEED/FMT/CHANNELS ioctls reset each other — set SPEED→FMT→CHANNELS and read back the actual rate
  with `SOUND_PCM_READ_*`. The hardware is permanently mono. See
  `scummvm-roomwizard/backend-files/oss-mixer.cpp` and `SYSTEM_ANALYSIS.md#34-audio`.
- **32-bit ARM:** `sizeof(long) == 4`. Never do `(now.tv_sec - 0) * 1000000L` — it overflows. Baseline
  timers to current time, not epoch 0.
- **Firmware / boot edits.** There is **no boot-time MD5 check** of the kernel and no signing — the
  only gate on `uImage-system` is its uImage header + data CRC (which `usb_host/patch_dtb.py`
  recomputes correctly). U-Boot has no `saveenv`, so the environment cannot be persisted or corrupted.
  **Rules:** never write `/dev/mtd*`; never overwrite `mlo`, `u-boot.bin` or `ctrlblock.bin` on p1;
  stage experimental kernels under a *new* filename and leave `uImage-system` alone — `bootcmd` is
  hardcoded to it, so a power cycle is a free undo. Observe that and JTAG never comes up. Detail and
  recovery procedure: `SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery`.

## Cross-component build rules

Learned the hard way in one component, applicable to all of them. **Full detail, measurements and the
flags each component actually uses: `SYSTEM_ANALYSIS.md#6-building-for-this-device`.**

- **Never use `--whole-archive` with `-lpthread` for static ARM builds.** It pulls in glibc 2.31's
  pthread init, which calls `clock_gettime64` (ARM syscall 403, kernel 5.1+). This kernel is 4.14.52,
  so it gets `-ENOSYS`, then dereferences a NULL VDSO pointer: **SIGSEGV before `main()`**, no output,
  no log. The `dmesg` signature is `PC is at 0x40` with `r0 : ffffffda`. Plain `-lpthread` is fine; the
  native C apps escape it only because they never link pthread at all.
- **Verify build artifacts on disk, not config flags.** Generated `config.mk`/`config.h` go stale — a
  leftover `USE_PNG = 1` once made the build compile `image/png.cpp` with no `libpng.a` present. Test
  for the `.a`, not the flag.
- **Cross-compiled dependencies must be built from source.** This WSL cannot do armhf multiarch, which
  is why ScummVM and the VNC client each build their own zlib/libpng/libjpeg into a local prefix. With
  `-mfpu=neon`, libpng needs `-DPNG_ARM_NEON_OPT=0`.
- **No `SCHED_RR` audio thread.** On this single 600 MHz core an RT audio thread starves the main
  thread and you get a black screen. `SCHED_OTHER` plus the ~500 ms OSS ring is enough.
- **The DSS *can* scale in hardware** — three overlay planes with independent input/output sizes at
  `/sys/devices/platform/omapdss/`, driven from sysfs with no kernel work. The only graphics
  acceleration on this GPU-less part; unused so far (`IMPROVEMENT_PLAN.md` F2).
- Before optimising a software renderer, read `SYSTEM_ANALYSIS.md#65-software-rendering-techniques-that-paid-off` —
  seven techniques that together took ScummVM from 80 % to 32 % CPU, and which the VNC client reused.

## Architecture

**Deployment model — strict separation of concerns:**

| Layer | Script | When |
|---|---|---|
| Front door (menu over everything below) | `roomwizard.sh` | whenever you'd rather not remember the flags |
| SD-card commissioning | `commissioning/card-prep.sh` | once, offline |
| System setup (cleanup, init, audio, time-sync, mDNS) | `commissioning/provision.sh` | once, over SSH |
| **All of the above in one offline pass** | `commissioning/commission-offline.sh` (rules: `device-files/clean-rules.conf`, executor: `lib/rw-clean.sh`) | once, offline, for *delivery* |
| Build + deploy all components | `deploy-all.sh` | per deploy |
| Per-component build/deploy/manifest | `*/build-and-deploy.sh` | per component |
| Build + stage + publish an offline bundle | `release.sh` (layout: `lib/rw-bundle.sh`) | per release |
| App respawn loop at boot | `device-files/roomwizard-app` (`/etc/init.d/roomwizard-app`) | every boot |

`roomwizard.sh` is a **composition layer with no logic of its own** except `wait_for_ssh`: every item
execs one of the scripts below it, and all of them stay non-interactive when called directly. It polls
**SSH, not ping**, because ping answers while `sshd` is still starting — exactly the window that
produces a spurious "Cannot reach". The SSH phases are not merged into one script because the cleanup's
targets span four partitions that only a booted kernel assembles into one tree; the argument is in
`COMMISSIONING.md`. **`commissioning/commission-offline.sh` is the answer to that, not an exception to it** — it
mounts all four by position and maps every device-absolute path onto the right one, which is the work
the SSH phases get for free, and it orchestrates `commissioning/card-prep.sh` rather than restating its
two prompts. The SSH path stays as the verified development loop.

**System setup is done once by `commissioning/provision.sh`** — component deploy scripts must not duplicate it.
`deploy-all.sh` auto-discovers components (any subdir with a `build-and-deploy.sh`), always runs
`native_apps` first, then sets `app_launcher` as the default boot app.

**Host name is set in three files, by one script.** `commissioning/set-hostname.sh` writes `/etc/hostname`, rewrites
`/etc/hosts` — because the vendor image maps the device's own name on a **non-loopback** line to an
unreachable address, so units can collide on a name and each resolves its own name wrongly — and sets
`/etc/dhclient.conf`'s `send host-name`, which is the one a DHCP server and therefore a router's device
list reads. It keys the `/etc/hosts` removal on the name it reads from `/etc/hostname`, never a
hardcoded one (the shipped name varies per image; `RW09` and `null` are both real), and it keys the
dhclient edit on the **directive**, not the old name, because the two disagree on real units. Each
rewrite has a negative control: it refuses to write a `/etc/hosts` that lost `localhost`, or a
`dhclient.conf` that would not announce the new name. Called from every bring-up path, so they cannot
drift, and `--hostname` does **not** reboot — which is what makes it usable on a unit in service as a
live display. ⚠️ **None of the three is the last word until the vendor stack is gone** — the boot-time
regenerator above overwrites all of them, so on an uncleaned unit the name must also be written to
`/home/root/data/websign/net.hostname` (`IMPROVEMENT_PLAN.md` D7b). `commissioning/commission-offline.sh` deletes
`websign` in the same pass that sets the name, which removes that window instead of patching it.

⚠️ **Never identify a partition by filesystem UUID.** A UUID is assigned at mkfs time, so it names one
*card*: units are mkfs'd independently at the factory and two RoomWizards on identical firmware share
**none** of their four UUIDs. Nothing on the device consumes one either — `root=/dev/mmcblk0p6` and
`/etc/fstab`'s `/dev/mmcblk0p{2,3,5,7}` are both by position. `lib/rw-identify.sh` is the one
implementation, sourced by `commissioning/card-prep.sh` and `commissioning/clone-to-32gb.sh`: **content** for a
mounted rootfs (`rw_is_rootfs`), the **partition table** for a disk (`rw_is_card_disk`), and
**position** for which partition holds which tree (`rw_card_partitions` → `RW_PART_ROLES` = p6 root,
p2 data, p3 log, p5 backup). It excludes `/` from its scan on purpose — a content scan that selected
the dev host's root would rewrite this host's `/etc/shadow`; `rw_host_root_disk` /
`rw_is_host_root_disk` are the resolved veto for the disk-level equivalent. Regression:
`tests/rw_identify_test.sh` (host-only, no card, no root; 37 cases).
Reasoning: `COMMISSIONING.md` → *Finding the card*; measurements: `SYSTEM_ANALYSIS.md#42-partitions`.

⚠️ **p1 is deliberately absent from `RW_PART_ROLES`, and a test asserts its absence.** Nothing can
reach `mlo`, `u-boot.bin`, `ctrlblock.bin` or `uImage-system` through those functions — which is a
stronger guarantee than every caller remembering not to, and it is what keeps a power cycle a free
undo. p4 (extended container) and p7 (swap) are absent for the same reason: nothing to mount.

⚠️ **A rootfs mounted offline shows `/home/root/{data,log,backup}` as three EMPTY directories** — they
are mount points for p2/p3/p5. An offline tool that mounts only p6 sees no `websign/` (the network
regenerator's input, p2), no logs (p3) and no 472 MB upgrade payload (p5), and would report success
having touched none of them. Use `rw_mount_card` / `rw_check_card_mounts`; the latter's negative half —
"a rootfs where `data` was expected means the partitions are in the wrong order" — is the half that
catches the mistake that makes every later path resolve under the wrong tree.

**A script's executable bit lives in the git index, so one bad commit breaks every fresh clone.** All
`*.sh` are `100755` there — check with `git ls-files -s -- '*.sh'` after adding one. Belt and braces:
`roomwizard.sh` and `deploy-all.sh` invoke their children as `bash <script>`, never `./<script>`, so a
tree whose modes are wrong still works. Note that a missing `+x` **cannot be reproduced on this host**
— `/mnt/c` is DrvFs and discards `chmod`.

**Stopping what is running belongs to the init script, and it matches on the executable.**
`/etc/init.d/roomwizard-app stop` is the only implementation; the three component scripts call it and
**must not carry a `killall` of their own.** The reason is not tidiness: a name-based rule cannot see
the app that `app_launcher` *started*, and that grandchild is normally the process holding `/dev/fb0` —
its basename appears in no config file. `app_pids()` walks `/proc/*/exe` against the three deploy
directories instead, because the exe link is the only identity neither chosen by the process nor
limited to the configured app. Two consequences: **`commissioning/provision.sh <ip>` is what pushes that
script**, so a `do_stop()` change does not reach a device until it is re-run; and to see what is
running use `/etc/init.d/roomwizard-app status`, because `ps w` on this busybox lists only processes
with a TTY. See `SYSTEM_ANALYSIS.md#53-app-launcher-and-manifests`.

**App launcher + manifests.** The boot init script reads `/opt/roomwizard/default-app` and respawns
that binary whenever it exits (so exiting a game returns to the launcher). `app_launcher` scans
`/opt/roomwizard/apps/*.app` manifests (INI: `name=`, `exec=`, `icon=` PPM P6, `args=` one of
`fb,touch`/`fb`/`touch`/`none`) and renders them as a touch grid. Each component's deploy script writes
its own `.app` manifests + PPM icons — this is how projects plug into the launcher without a central
registry.

**native_apps shared C library (`native_apps/common/`)** — every game and tool links these:

- `framebuffer.c` — double-buffered 800×480 rendering, bpp-aware primitives, sprite blit
- `gamepad.c` — **unified input abstraction** across touch + USB keyboard + USB mouse + Xbox gamepad,
  mapped to abstract buttons (`BTN_ID_UP`…`BTN_ID_BACK`). New apps should use this rather than reading
  evdev directly. Configurable via `/etc/input_config.conf`.
- `touch_input.c` (the raw→panel→logical map), `touch_calib.c` (**the** implementation of the
  calibration fit — linked only by `device_tools` and `touch_raw`), `hardware.c` (LED/backlight sysfs
  plus the non-blocking `LedPulse`), `common.c` (buttons, `ModalDialog`, shared welcome/game-over
  screens), `ui_layout.c` (layouts + `ScrollableList`), `audio.c` (OSS beeps/tones), `config.c`
  (`/opt/games/rw_config.conf`), `keyboard.c` (on-screen touch keyboard), `highscore.c`, `ppm.c`,
  `logger.c`.

ScummVM has its **own independent** evdev input + OSS audio implementation (not the native_apps common
lib) but applies the same Cortex-A8 / OSS fixes.

## Device paths worth knowing

- `/opt/games/` — native binaries · `/opt/roomwizard/apps/` — `.app` manifests ·
  `/opt/roomwizard/icons/` — PPM icons · `/opt/roomwizard/default-app` — boot target
- `/dev/fb0` (framebuffer) · `/dev/input/event*` (evdev) ·
  `/sys/class/leds/{red_led,green_led,backlight}/brightness` (0–100)
- `/dev/dsp` (OSS audio, GPIO12 must be HIGH to unmute the speaker) · `/dev/watchdog` (60 s HW)
- `/etc/touch_calibration.conf` (touch curve + bezel) · `/etc/input_config.conf` (input mapping)
