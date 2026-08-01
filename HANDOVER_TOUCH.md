# Handover — close out touch calibration

**Transient working document.** Like `TOUCH_REACH_INVESTIGATION.md` before it, this is not one of
the three standing docs. Delete it when the work below is done; anything durable belongs in
`SYSTEM_ANALYSIS.md` (facts), `IMPROVEMENT_PLAN.md` (work) or `CLAUDE.md` (first-edit rules).

## Opening prompt for the next session

> Read `HANDOVER_TOUCH.md`, then `SYSTEM_ANALYSIS.md#33-touch` and `IMPROVEMENT_PLAN.md` B3–B3d.
> We settled digitizer reach on 2026-07-31; the job now is to consolidate the calibration tooling
> and delete what the finding made obsolete. Work through tasks 1–3 in that file. Nothing needs
> re-measuring unless you disagree with the numbers — the raw capture is
> `touch_raw-2026-07-31-rw09.tsv` and the tool that produced it is `/opt/games/touch_raw` on
> 192.168.50.73.

---

## Where things stand

**Settled 2026-07-31.** Digitizer reach differs per axis:

| | X | Y |
|---|---|---|
| raw 0..4095 covers panel | **−3 … 801** (overshoots the glass) | **29 … 449** |
| inset | **none** | **~30 px top and bottom** |

Consequence, and the one rule to carry forward: **touchable is smaller than visible, on Y only.**
The logical surface is panel y 15…464; the touchable band is 29…449. So the first and last ~15 rows
of the 450-row drawing surface are visible but dead to touch, while every column works.

The old "~10 px left/right" figure was an artifact of a calibration fit whose crosshairs sit inside
the compressed band. Full detail: `SYSTEM_ANALYSIS.md#33-touch`.

**Already done, do not redo:** `touch_raw` written/built/deployed; `touch_trace` and `touch_inject`
added to `build-and-deploy.sh` (neither was built before); `.73` calibration corrected to
`17 4084 -279 4382` (backup `/etc/touch_calibration.conf.bak1`); `SYSTEM_ANALYSIS.md`,
`CLAUDE.md`, `native_apps/CLAUDE.md`, `native_apps/README.md`, `IMPROVEMENT_PLAN.md` all updated.

---

## Task 1 — Retire `TOUCH_REACH_INVESTIGATION.md`

It has been rewritten as a resolved record and every fact in it now has a home elsewhere. It is
**untracked in git**, so deletion is unrecoverable — confirm before removing.

Check before deleting: does it contain anything not present in `SYSTEM_ANALYSIS.md#33-touch`,
`IMPROVEMENT_PLAN.md` B3a–B3d, or `touch_raw-2026-07-31-rw09.tsv`? The hypothesis post-mortem (H1–H4
disposition) is the only candidate; if it seems worth keeping, it belongs as a short "why the old
numbers were wrong" note in `SYSTEM_ANALYSIS.md`, not as a fourth doc.

Also decide on `touch_trace-2026-07-31-rw09.tsv` — the older 698-sample edge drag, superseded by
the `touch_raw` capture. Probably delete; it is the evidence for a conclusion we now know was half
wrong.

## Task 2 — One `touch_raw`-based flow that writes the whole config file

**The 9-tap calibration exists twice, independently:**

| Copy | Location | Notes |
|---|---|---|
| `device_tools` Calibration tab | `device_tools/device_tools.c:1739` `CALIB_TAP_INSET 40`, `calib_targets()`, `calib_rawx[9]` | this is the one users reach, via Set Screen |
| standalone | `tests/unified_calibrate.c:29` `TAP_INSET 40` | same design, separate code |

Both have the same defect (**B3a**): 40 px inset puts the corner and edge-mid crosshairs *inside*
the compressed band, so the fit slope comes out shallow and extrapolates outside 0..4095. Fixing it
in one place and not the other would be worse than either.

`SCREEN EDGES` (`device_tools.c` `CALIB_BEZEL`) is the third piece — it measures line 2 by drawing a
frame on the *logical* edge and nudging margins, i.e. it measures the bezel through the bezel.

**Target design.** One flow, `touch_raw`'s approach, producing both config lines:

1. **Reach + calibration (line 1)** — tap interior targets only, per-axis least squares via the
   existing `touch_fit_axis_range()`. Reference thresholds that worked: ≥100 px from each end on X,
   ≥80 px on Y. Keep the outer crosshairs on screen as a *check* if you like, but exclude them from
   the fit. Verify against the captured data: the corrected fit is `X 17..4084 / Y −279..4382`,
   residuals ≤2 px, edge probes at x=20/780 predicted within 7 px.
2. **Bezel (line 2)** — must be measured on the **full panel** with the bezel zeroed
   (`fb_set_bezel(fb,0,0,0,0)`), which is exactly what `touch_raw` already does. Reach is measured
   by *touching*; the bezel is measured by *looking* — no amount of touching reveals which pixels
   the plastic hides. `touch_raw`'s existing 10 px edge ladders already get within 10 px by eye;
   finish the job with 2 px steps and a "tap the lowest number you can read" interaction.
3. **Reach report** — derive the touchable rectangle from (1) and show it against (2), so the
   operator sees the visible-vs-touchable gap directly instead of rediscovering it.

**Build this while you are in there** (`IMPROVEMENT_PLAN.md` B3): today a bad fit can wedge the
device — phase-2 ACCEPT/REDO are hit-tested *through the new calibration* (`device_tools.c:1827`),
so if the fit is bad you can press neither, and there is no RESET. Give the new flow a RESET, a
phase-2 timeout that reverts, and hit-testing that uses the *old* mapping until the operator
accepts. `touch_raw`'s backup-to-`.bakN` before writing is a working pattern to reuse.

⚠️ If you add the `EVIOCGABS` overlap sanity check, **do not reject values outside 0..4095**. The
correct Y range genuinely spans ~14 % wider than the hardware range, because the inset is real.

Also fix **B3b** while touching the summary: `touch_raw` prints a single global H1/H4 verdict driven
by the worst axis, which on this panel reads `H4` and hides the fact that X is clean. Per-axis.

## Task 3 — Remove what has no benefit

**Source with no build rule and no binary on the device** (only orphan `.hidden` markers exist):

| File | Verdict |
|---|---|
| `tests/touch_calibrate.c` | superseded by `unified_calibrate`, itself superseded by task 2 — **delete** |
| `tests/touch_debug.c` | superseded by `touch_raw`'s live mode — **delete** |
| `tests/touch_test.c` | superseded by `touch_raw`'s live mode — **delete** |
| `hardware_test/pressure_test.c` | **keep for now** — `SYSTEM_ANALYSIS.md#33-touch` names it as the way to answer whether `ABS_PRESSURE` actually varies. Either run it to a conclusion and then delete, or leave both in place. Do not delete it while the doc still points at it. |
| `tests/unified_calibrate.c` | delete **only if** task 2 folds it into `device_tools`; until then it is the standalone entry point |
| `tests/touch_trace.c` | keep — it is the only view of the *calibrated* mapping, complementary to `touch_raw` |

**The `.hidden` marker loop creates markers for binaries that do not exist**
(`build-and-deploy.sh`, the `for name in touch_test touch_debug touch_inject touch_raw …` line).
`ls /opt/games | grep touch` currently shows `touch_test.hidden`, `touch_debug.hidden`,
`touch_calibrate.hidden`, `pressure_test.hidden` with no corresponding binary. Prune the list to
what is actually deployed, and clean the stale markers off `.73` and `.53`.

`native_apps/Makefile` is dead (`IMPROVEMENT_PLAN.md` B16) and still lists `watchdog_feeder` and
moved files. Unrelated to touch, but it is the other "tool that has no benefit" in this directory.

---

## What else, to close this once and for all

Roughly in priority order.

1. **Decide the Y dead-band cure — this is the real open design question** (`IMPROVEMENT_PLAN.md`
   B3c). Either bezel margins `30/30` (logical surface becomes 800×420; every logical pixel is both
   visible and touchable, the safe-area invariant becomes true again, costs 30 rows and a re-layout
   pass) or a band-limited edge stretch in `scale_coordinates()` (keeps 450 rows, warps touch by up
   to 30 px in the outer band). Do not rescale the whole axis. Everything else here is tidying; this
   one changes behaviour.
2. **Give apps a name for the touchable rectangle.** `SCREEN_SAFE_*` currently equals the whole
   logical screen, which is now false for touch. Add `SCREEN_TOUCH_TOP/BOTTOM/LEFT/RIGHT` in
   `framebuffer.h` derived from the calibration, so app authors stop having to remember "15 px".
   If you take option (a) in item 1 this becomes unnecessary — which is an argument for (a).
3. **Audit interactive targets against the dead bands.** Anything in the top or bottom 15 logical px
   is unpressable today. Sweep the launcher, `device_tools`, `game_selector` and the games.
4. **Rebuild and redeploy ScummVM.** It links its own `touch_input.o`, so it keeps whatever it was
   built with. Then re-check the original complaint that started all this — the pointer not reaching
   a corner. Horizontal should now be fixed outright; vertical will still stop ~15 logical px short
   until item 1 is decided.
5. **Repeat the measurement on `.53`** (the old E5). ~30 px is currently a *one-unit* figure. Run
   `/opt/games/touch_raw`, capture pass, compare. If the second unit agrees, promote the number in
   `SYSTEM_ANALYSIS.md` from "measured on RW09" to a panel-model property.
6. **Portrait mode inverts the problem, and nobody has looked.** `fb_init()` swaps width/height and
   `scale_coordinates()` rotates *after* the linear map, so the Y sensor inset becomes a **left/right**
   dead band in app coordinates. Any rule written as "top and bottom" is wrong in portrait, and
   calibration is landscape-only by design. Decide whether portrait is supported enough to care, and
   write the answer down either way.
7. **`touch_inject` argument validation is now stale.** It clamps args to 0..4095
   (`tests/touch_inject.c`), which mirrors the hardware but no longer matches the calibration: the
   memory note claiming "the bottom ~34 px cannot be expressed" was written against the old bad
   calibration. Either make it read the config and accept panel coordinates, or document precisely
   what it takes.
8. **The right-edge loose end.** The x=780 probe reads +6 px high, so raw bunches slightly in the
   last ~20 px and probably pins near panel 795 rather than 801 — consistent with the pin flag
   lighting while a finger is still visibly on screen there. A handful of pixels; measure it or
   explicitly write it off.
9. **Unrelated but noticed:** `SYSTEM_ANALYSIS.md:1206` still repeats the "libgcc carries ~45
   `sdiv`/`udiv` in unreachable libc internals" claim that `CLAUDE.md` records as **corrected** on
   2026-07-30 (those are substring matches on software-divide helper *names*, not instructions).
   Stale fact in a standing doc; fix it while you are in the file.

## Verification for any of this

```bash
cd native_apps && ./build-and-deploy.sh                 # zero warnings + ARM-safety gate
./build-and-deploy.sh 192.168.50.73

ssh root@192.168.50.73 '/etc/init.d/roomwizard-app stop; /opt/games/touch_raw'
#   live mode: drag every edge; the pin flag should light only where the numbers say it should
#   capture pass: 11 targets x 3 taps + 4 bezel presses -> per-axis table
scp root@192.168.50.73:/tmp/touch_raw.tsv .
ssh root@192.168.50.73 '/etc/init.d/roomwizard-app start'
```

Regression baseline: a correct interior-only fit on RW09 reproduces `X 17..4084`, `Y −279..4382`
to within a few raw counts. If a change makes the X endpoints drift outside 0..4095 again, the
fit has picked up edge samples.
