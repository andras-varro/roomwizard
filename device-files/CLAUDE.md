# device-files/CLAUDE.md

What is installed onto a device **verbatim**, plus the two data files that decide what a bring-up
removes and installs. Loaded when you work in `device-files/`.

Anything installed by more than one path lives here, never in a heredoc: `roomwizard-app` (the boot
init script — it carries the name it is *deployed* as rather than a `.sh` one), `disable-steelcase.sh`,
`audio-enable`, `time-sync`, `99-security.conf`, the three USB scripts `enable-usb-host.sh` /
`usb-host` / `xpad-modules`, plus `clean-rules.conf` and `provision-rules.conf`. Both
`commissioning/provision.sh` (over SSH) and `commissioning/commission-offline.sh` (onto a mounted card)
install those same bytes, and **neither decides what to install or delete — both read the rules.**

⚠️ **`.gitattributes` pins `device-files/**` to `eol=lf`.** The init scripts have no `.sh` extension —
`/etc/init.d/audio-enable` is the name the `rc5.d` link points at — and a CRLF shebang is rejected by
BusyBox as a misleading "no such file or directory". Never bulk-edit a file here with a python script;
it rewrites the whole file's line endings. Use `Edit`, or `sed -i`.

## Two data files, one shape

`clean-rules.conf` (4 fields) says what is *removed*; `provision-rules.conf` (6 fields) says what the
device ends up *with*. Both make the reason **mandatory and last**, both are compiled to a plan by a
library in `lib/` (see `lib/CLAUDE.md`), and both are executed twice — once with `/` over SSH, once
under `$BASE/root` offline. A line with fewer fields than declared is an error, because a missing
reason is how a delete gets in without anyone having to justify it.

### `clean-rules.conf` — `<type> <group> <path> <reason>`

Record types `scope` / `keep` / `delete` / `truncate`. This is the one place a keep or a delete is
decided.

- **`scope` is a whitelist sweep**, which is what makes an unrecognised vendor service on a unit nobody
  has inspected removed *by construction*. It never recurses, so a kept directory's contents are never
  examined.
- **`--remove` and `--deep-clean` are one mechanism differing by one group.** All nine `scope` records
  are in the `sweeps` group, so `--remove` is exactly `--deep-clean --keep-sweeps`: the named vendor
  stacks go, and a path no `keep` names stays.
- ⚠️ **A rule the sweeps would cover anyway may still need to be named.** The eight vendor logs under
  `/home/root/log` are named explicitly *and* swept, because the sweep is in `sweeps` and `--remove`
  runs without it. Dropping them looks safe against `--deep-clean`'s plan and loses them from
  `--remove`'s — so **check a fold against both plans**, not the default one.
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

### `provision-rules.conf` — `<type> <group> <mode> <target> <source> <reason>`

Types `install` / `link` / `link-opt` / `unlink` / `touch` / `backup` / `directive` / `dropline`. Every
column means one thing for every type; `-` is the explicit not-applicable and an *empty* field is an
error.

- **Every `install` record has a `device-files/` source** — that is the check that a new file is in the
  right place.
- ⚠️ **A `link` source must be RELATIVE** — `../init.d/time-sync`, never `/etc/init.d/time-sync`. An
  absolute symlink target is correct on a running device and *dangling on a mounted card*, and a
  dangling `rc5.d` link is skipped in silence at boot. It is the one defect this file could introduce
  that nothing downstream would catch, so validation rejects it.
- ⚠️ **`rw_provision_check_keeps` asserts every boot link is on `clean-rules.conf`'s keep list.** A link
  installed but not whitelisted is deleted by the next `--deep-clean`, so the unit boots right once and
  loses it. That pairing used to be a comment in *both* files asking a human to remember.
- **`usb` is an optional group**, compiled by `usb_host/build-and-deploy.sh` through
  `rw_provision_plan_component`. The three USB device scripts and the two `rc5.d` links are ordinary
  `usb`-group records; **only the 500 mA power budget touches p1**, and that is `lib/rw-usbpower.sh`'s
  job, not a record here.

Modes are **declared** in these files, never read off disk — `/mnt/c` reports every file 0777 and
discards `chmod`, so `stat -c %a` here is a constant, not a measurement.

## `roomwizard-app` — stopping what is running

`/etc/init.d/roomwizard-app` is the app respawn loop, and **its `stop` is the only implementation of
"stop what is running".** The three component scripts call it and **must not carry a `killall` of their
own.** The reason is not tidiness: a name-based rule cannot see the app that `app_launcher` *started*,
and that grandchild is normally the process holding `/dev/fb0` — its basename appears in no config
file. `app_pids()` walks `/proc/*/exe` against the three deploy directories instead, because the exe
link is the only identity neither chosen by the process nor limited to the configured app.

Two consequences: **`commissioning/provision.sh <ip>` is what pushes that script**, so a `do_stop()`
change does not reach a device until it is re-run; and to see what is running use
`/etc/init.d/roomwizard-app status`, because `ps w` on this busybox lists only processes with a TTY.
See `SYSTEM_ANALYSIS.md#53-app-launcher-and-manifests`.

## Redeploying anything here

Changing a file in this directory does **not** go out with a component deploy. Only
`./commissioning/provision.sh <ip>` (which ends in a reboot) or `commissioning/commission-offline.sh`
installs it. The exception is the three **`usb`-group** scripts, which
`cd usb_host && ./build-and-deploy.sh <ip>` also installs, and which need no reboot.

## Regressions

`tests/rw_clean_test.sh` and `tests/rw_provision_test.sh` (both host-only, no card, no root), plus
their `tests/measure_*_sabotage.sh` harnesses. What those suites structurally cannot see is in
`tests/CLAUDE.md` — read it before trusting a green run over a change here.
