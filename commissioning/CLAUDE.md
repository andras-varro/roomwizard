# commissioning/CLAUDE.md

The bring-up scripts. Loaded when you work in `commissioning/`.

None of these is an answer to "what do I run" except through `roomwizard.sh` at the repo root. The
libraries they drive are documented in `lib/CLAUDE.md`; the rules files they read in
`device-files/CLAUDE.md`; the operator-facing walkthrough and the argument for the phase split in
`COMMISSIONING.md`.

| script | when |
|---|---|
| `card-prep.sh` | bring-up 1: SD-card prep, offline, sets host name |
| `provision.sh <ip>` | bring-up 2: SSH cleanup + init service + SW-watchdog bypass |
| `commission-offline.sh --bundle <tar.gz\|dir>` | all of the above, offline, **one** boot, for delivery |
| `set-hostname.sh` | the one writer of the three name files |
| `clone-to-32gb.sh` | optional card upgrade (`SD_CARD_UPGRADE.md`) |
| `backup.sh <ip> [<out>]` | before an update or reflash: the unit's BACKUP set (`lib/rw-state.sh`) to `backups/` — read-only on the unit, archive holds secrets |
| `restore.sh <ip> <tar.gz> [--dry-run]` | after: refuses unsafe or non-BACKUP paths, `roomwizard-app stop`, `tar -o` over `/`, SSH modes, `start` |

## Both bring-up paths CLEAN BY DEFAULT, and neither writes p1

`provision.sh <ip>` with no flags deep-cleans the vendor stack, exactly as `commission-offline.sh`
does — the point being that the two paths leave the same unit. `--no-clean` is the opt-out,
`--keep-<group>` and `--no-<group>` the partial ones, and `--status` / `--hostname` still change
nothing and never ask. Neither script mounts or writes p1.

**Both `--remove` and `--deep-clean` read their decisions from `device-files/clean-rules.conf`**, and
differ from each other only by that file's `sweeps` group; `--remove` is the *narrower* selector rather
than the only way in.

⚠️ **`--no-clean` must be matched BEFORE the `--no-*` glob** in both scripts' argument loops: `case`
takes the first match, so an arm placed after it is unreachable and the operator gets
`Unknown provision group: clean`.

**`--ssh-auth=password|key` is on both scripts, default `password`**, and `provision.sh --sshd-only`
re-applies just the sshd records with no clean or reboot. ⚠️ **Key-only must never be applied
without proof of a key** — online `rw_sshd_key_login_ok` runs before the consent question, offline
`card-prep.sh` gets `RW_SSH_KEY_REQUIRED=1` and its `authorized_keys` must be newer than a marker taken
before it ran. A conflicting `--no-sshd` is refused, not resolved. Mechanism: `lib/rw-sshd.sh`'s header.

**Cleanup, bloatware removal and the boot service all live in `provision.sh`** — never in a component
script. `deploy-all.sh` and the four `*/build-and-deploy.sh` must not duplicate any of it.

## The consent gate is one question with two implementations

Both irreversible steps — the clean and the p1 write — are covered by one question, asked once, before
the first write, naming them separately, and placed *after* `--status`/`--hostname` have had their
chance to exit. ⚠️ **The two paths then diverge on a non-TTY, deliberately, and neither may be
"simplified" into a silent proceed or a silent skip:**

| path | implementation | on a non-TTY |
|---|---|---|
| `provision.sh` | `ask_consent()` at `provision.sh:365`, called at `:802` | **proceeds**, printing an unmissable banner saying what nobody answered |
| `commission-offline.sh` | its own inline backup question at `:331` | **refuses** — the bare `read` sees EOF, the answer is not `yes`, `exit 1` |

`provision.sh` proceeds because the defect it replaced was an unguarded `read` whose EOF *cancelled the
clean and returned 0*, so a scripted run silently did not clean while the operator believed the default
did. `commission-offline.sh` refuses because it needs root and writes a card that is about to be
handed over. **What both must never do is skip the clean and report success.**

**The one sanctioned unattended run is `commission-offline.sh --unattended`**, whose only caller is
`rootfs/make-card-image.sh --bundle` (an image of our own root, mounted by the caller). It skips the backup
question and `card-prep.sh`, reads `/dev/null`, and is **refused unless `--base`, `--no-clean` and
`--no-sshd` are all given** (the guard at `commission-offline.sh:287-298`): the clean would delete our
`rcS.d` links, and skipping a clean silently is the failure above, so the skip must be named.

## `commission-offline.sh` verifies what it installed, on the card

It md5s every **installed** file against the bundle manifest, asserts `+x` (real ext4 honours it, so
that check is a measurement offline and cannot be one on `/mnt/c`), runs `check-arm-safe.sh` over the
**downloaded** binaries, asserts every `.app`'s `exec=`/`icon=` and that `default-app` names one of
them, and `dash -n`s every `/bin/sh` script it wrote.

⚠️ Three ways that verification can lie, all guarded: `dash -n` misses bashisms (`[[` parses as a
command name); a missing `arm-linux-gnueabihf-objdump` is a **refusal**, not a pass, so the caller
counts the ELF candidates itself and says loudly what it did not check (`--arm-check=skip` is the
deliberate override); and a **stripped** binary cannot be gated at all, so the checker returns 2
("could not judge") and the installer proceeds with a loud block naming the count. `scummvm` and
`vnc_client` ship stripped, so **every full bundle** takes that path — the sound verdict is the
build-time one, on the unstripped artifact. Detail: `tests/CLAUDE.md`, and `IMPROVEMENT_PLAN.md`.

## `card-prep.sh` is a step of the offline pass, not an alternative to it

The handover carries **two** variables, and they are separate on purpose:

- `ROOTFS` skips its own card detection.
- `RW_COMMISSION_ORCHESTRATED` suppresses its closing banner and the `NEXT_STEPS` block it reads out of
  `COMMISSIONING.md`, which tells the operator to run `provision.sh` and `deploy-all.sh` — both already
  done by the time it prints.

⚠️ **`ROOTFS` alone also means "I mounted the card myself"**, and that operator does still need the
next steps. Suppressing on `ROOTFS` is the obvious wrong fix and passes every other case.

⚠️ **`card-prep.sh` alone does not clean, so phase 1 by itself can produce a unit phase 2 cannot
reach** (measured 2026-08-08). If `/home/root/data/websign/net.mode` is `manual` the unit takes a
static address and sends no DHCP request — it appears in no router lease list and SSH is impossible
until the card is edited offline. So `card-prep.sh` mounts **p2 read-only**, reads `net.mode`, and
offers to remove the p6 link `etc/rcS.d/S60networkmanager` — `[ -t 0 ]`-gated, keeping it on a
non-TTY, and `unmeasured` is never folded into "nothing to do".

## Host name is set in three files, by one script

`set-hostname.sh` writes `/etc/hostname`, rewrites `/etc/hosts` — because the vendor image maps the
device's own name on a **non-loopback** line to an unreachable address, so units can collide on a name
and each resolves its own name wrongly — and sets `/etc/dhclient.conf`'s `send host-name`, which is the
one a DHCP server and therefore a router's device list reads.

It keys the `/etc/hosts` removal on the name it reads from `/etc/hostname`, never a hardcoded one (the
shipped name varies per image; `RW09` and `null` are both real), and it keys the dhclient edit on the
**directive**, not the old name, because the two disagree on real units. Each rewrite has a negative
control: it refuses to write an `/etc/hosts` that lost `localhost`, or a `dhclient.conf` that would not
announce the new name. Called from every bring-up path, so they cannot drift, and `--hostname` does
**not** reboot — which is what makes it usable on a unit in service as a live display.

⚠️ **None of the three is the last word until the vendor stack is gone.** `/opt/sbin/networkmanager`
regenerates all of them from `/home/root/data/websign/net.*` at every boot
(`SYSTEM_ANALYSIS.md#35-network-and-power`), so on an uncleaned unit the name must also be written to
`websign/net.hostname` too. `commission-offline.sh` deletes `websign/` in the
same pass that sets the name, which removes that window instead of patching it. The vendor's own
validator **rejects hyphens**, so prefer `rwtest` to `rw-test` on anything still carrying the vendor
stack.

## Rooting out the software watchdog

The Steelcase software watchdog reboots the device ~every 70 min in game mode. It is a cron job
(`/opt/sbin/watchdog/watchdog.sh`), and **the clean deletes the whole directory** — a `delete base`
record, and `base` is not an optional group, so no flag and no mode leaves a rebooter on disk.
`device-files/disable-steelcase.sh` supplies the two weaker layers: a freshly written crontab, plus
`touch /var/watchdog_test` as its *first* command, deliberately ahead of every fallible line.
`provision.sh <ip>` deploys and runs it; `/etc/init.d/roomwizard-app` re-runs it on **every boot**. What
each layer is worth: [`SYSTEM_ANALYSIS.md` §3.13](../SYSTEM_ANALYSIS.md#313-watchdogs).

⚠️ **A device can be running an older copy than the repo's until `provision.sh` is re-run.**
`./commissioning/provision.sh <ip> --status` md5s both deployed scripts against the repo's and says
`matches repo` or `DRIFTED` per file (read-only, no reboot). **Check that before reproducing anything
against a device**, or you will draw conclusions about code the device is not running.

## Regressions

`tests/commission_offline_test.sh` (needs root and a staged bundle) and
`tests/commission_prep_test.sh` (host-only). The second covers `card-prep.sh`'s two host-side
decisions by **extracting them from the shipped file by line range** — neither is reachable by running
the script. Rules for both: `tests/CLAUDE.md`.
