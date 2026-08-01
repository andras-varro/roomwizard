# Touch reach at the panel border — **RESOLVED 2026-07-31**

**This file has done its job and is ready to be deleted.** Per its own original §1, the measured
facts have moved to [`SYSTEM_ANALYSIS.md#33-touch`](SYSTEM_ANALYSIS.md#33-touch) and the resulting
work to [`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md) B3a–B3d. The raw capture is preserved as
[`touch_raw-2026-07-31-rw09.tsv`](touch_raw-2026-07-31-rw09.tsv). Nothing below is a unique home for
any fact — it is kept only long enough for someone to check the reasoning against the conclusion.

---

## The answer: the question was mis-posed

The doc asked whether the untouchable border band was a physical sensor inset (H4) or an artifact of
the 9-tap calibration fit (H1). It framed them as alternatives for the *panel*. They are not
alternatives — **they are both true, on different axes.**

| | X | Y |
|---|---|---|
| interior-only fit → raw at panel 0 | **+17** | **−279** |
| interior-only fit → raw at panel max | **4084** (of 799) | **4382** (of 479) |
| raw 0..4095 covers panel | **−3 … 801** | **29 … 449** |
| bezel press, low edge | LEFT `raw 0` → panel **−3** | TOP `raw 22` → panel **31** |
| bezel press, high edge | RIGHT `raw 4095` → panel **801** | BOTTOM `raw 4095` → panel **449** |
| **verdict** | **H1** — no inset, the fit was wrong | **H4** — real, ~30 px each end |

`§3` link 3 ("the raw rectangle is smaller than the LCD active area") was the weak link, and it
breaks on X and holds on Y.

## How it was settled

A new tool, `native_apps/tests/touch_raw.c`, removed every layer between finger and pixel: raw range
reset to the `EVIOCGABS` values, `fb_set_bezel(fb,0,0,0,0)` so a drawn pixel *is* a panel pixel, and
the library configured into an identity map rather than bypassed. Then E1 in generalised form — fit
each axis from **interior targets only** (≥100 px from the ends on X, ≥80 px on Y), and test that fit
against edge probes and hard bezel presses that never entered it.

Session on RW09: 11 targets × 3 taps, 24 bezel presses, 1436 live drag samples, with timestamps
(closes the old E2). Interior-fit residuals were ≤2 px on both axes, so the panel is linear —
it simply does not span the full LCD height.

## Disposition of the original hypotheses

- **H1 (fit contaminated by its own edge samples)** — **confirmed, on X.** The 9 crosshairs are inset
  only 40 px, inside the compressed band; the fit slope came out shallow and extrapolated outside
  0..4095, inventing an inset that varied run to run. That is the entire explanation for the
  "6 px left / 14 px right" in the old §2c and for the 9 px disagreement between calibrations.
  Fix: `IMPROVEMENT_PLAN.md` B3a.
- **H2 (finger-centroid bias)** — **real but second-order, and it does not explain the Y result.**
  Visible in the bezel presses: nine hard presses on the top bezel returned `22, 76, 105, 111, 93,
  79, 90, 118, 47` — pressing flat puts a tall contact patch half over dead sensor, so the centroid
  is pushed inward and scatters. A constant bias shifts the intercept, not the slope, and the Y
  effect is a 14 % slope error. Prefer target taps over bezel presses when measuring.
- **H3 (the drag never reached the glass edge)** — **excluded.** The stale pre-bezel `touch_trace`
  that motivated this worry is now rebuilt and deployed; more directly, `touch_raw` draws on the full
  panel so what you aim at is where you aim, and the bezel presses agree with the fit to within 2 px.
- **H4 (real inset)** — **confirmed, on Y only.** ~30 panel px at each end, ~5.7 mm. The digitizer
  spans about 153.4 × 80.1 mm against an LCD active area of 152.4 × 91.4 mm: the electrode array
  matches the width and is ~11 mm short of the height.

## What changed as a result

- `/etc/touch_calibration.conf` line 1 on RW09 went from `-35 4167 -233 4335` to the measured
  `17 4084 -279 4382` (backup at `.bak1`). X reach went from `[6, 785]` to the full `[0, 799]`.
- **Touchable is smaller than visible, on Y.** Logical surface is panel y 15…464; touchable is
  29…449. So the first and last ~15 rows of the 450-row drawing surface cannot be touched, while
  every column can — the one place "the logical screen is the safe area" does not hold.
- Instrumentation defects from the old §6 are closed: `touch_trace` and `touch_inject` are now built
  and deployed by `build-and-deploy.sh` (neither was), and `touch_raw` asserts 32 bpp itself.

## Loose end

The x=780 edge probe reads **+6 px high**, so raw does bunch slightly in the last ~20 px on the
right and probably pins near panel 795 rather than 801. Consistent with the observation that the
pin flag lights while a finger is still visibly on screen at the right edge. A handful of pixels;
not measured precisely, and it does not change any decision.

E5 (repeat on `.53`) was never run, so ~30 px is a property of *this unit* until a second one agrees.
