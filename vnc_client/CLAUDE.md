# vnc_client — authoring guide

Rules and traps for working on the VNC client. User-facing usage, configuration and
troubleshooting are in [`README.md`](README.md). Device facts are in
[`../SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md). Open work is in
[`../IMPROVEMENT_PLAN.md`](../IMPROVEMENT_PLAN.md).

A lightweight libvncclient viewer that renders a remote desktop to the framebuffer and forwards
touch, USB keyboard and USB mouse back to the server.

## Build and deploy

```bash
./build-deps.sh                       # once: cross-builds zlib, libjpeg-turbo, LibVNCClient into deps/
./build-and-deploy.sh <ip>            # build + deploy (also: set-default, run)
```

**Use `build-and-deploy.sh`, not `make deploy`.** The Makefile's deploy target copies only the
binary and the config; it skips the `.app` manifest and PPM icon, so the app silently disappears
from the launcher grid. The Makefile is fine for compiling.

**Do not add a boot init script.** Boot is owned by `/etc/init.d/roomwizard-app`, which respawns
whatever `/opt/roomwizard/default-app` points at. `./build-and-deploy.sh <ip> set-default` is the
supported way to make this the boot app.

`vnc_client.conf` is gitignored because it holds a plaintext password; `vnc_client.conf.example`
is the tracked template. The deploy script installs it mode 0600 and will not overwrite an
existing config on the device.

## Framebuffer: this component runs 16bpp

`vnc_client.c` calls `fb_set_bpp(..., 16)` and the renderer writes `uint16_t` RGB565 directly
into the back buffer. Two consequences:

- **Screenshots need `--bpp 16`.** Native apps run 32bpp; the mode depends on which app ran last.
- **Do not call the `native_apps/common` draw helpers from this component.** `framebuffer.c`
  sizes its back buffer by hardware bpp but `fb_clear()` and `fb_draw_pixel()` unconditionally
  write 4 bytes per pixel. At 16bpp a single `fb_draw_text()` call overflows the back buffer by
  768 KB. This component survives only because it hand-writes `uint16_t` and never calls them.
  See `../IMPROVEMENT_PLAN.md` B1 — until that is fixed, the restriction stands.

## Shared code

Links `framebuffer.o`, `touch_input.o`, `hardware.o`, `config.o` and `logger.o` from
`../native_apps/common/`. It does **not** link `gamepad.o` — it has its own evdev scanner in
`vnc_input.c`, which is a duplicate of the one in `common/gamepad.c` and of a third copy in the
ScummVM backend.

That divergence is a live bug source: `MAX_INPUT_DEVICES` is 16 here but 32 in the other two, so
a USB keyboard enumerating as `/dev/input/event17` works everywhere except here. The
"clear `errno` before the read loop" hardening also exists only in the ScummVM copy. Prefer
linking `common/gamepad.c`; see `../IMPROVEMENT_PLAN.md` C1.

## Network robustness

The remote end is untrusted input. Two known gaps to keep in mind when touching this code:

- **Validate server-supplied geometry.** `vnc_malloc_fb()` and the scaling setup divide by and
  allocate from the server's announced width/height with no bounds check — a zero dimension is a
  SIGFPE. The bilinear X-LUT also overflows `int` for very wide desktops, producing a negative
  source index and an out-of-bounds read.
- **There is no dead-peer detection.** `WaitForMessage()` returning 0 is treated as "nothing to
  do", libvncclient sets no `SO_KEEPALIVE`, and nothing pings — so a silent TCP death leaves a
  stale frame on screen forever with no reconnect. Track the time of the last successful message
  and give up after a timeout.

Also: `rfbClientCleanup()` does not free `client->frameBuffer`. Free it yourself before calling
cleanup, or every reconnect leaks a full framebuffer (`../IMPROVEMENT_PLAN.md` B11).

## Rendering

Bilinear downscale (typically 2.4:1) plus BGR→RGB565 conversion, with partial updates driven by
the server's dirty rectangles. Techniques carried over from the ScummVM backend: precomputed
bilinear X-LUT (no per-pixel division), border-only clearing, row deduplication via an
L1-resident temp row (~57 % of scaled rows are duplicates), 16bpp to halve `fb_swap` cost, and a
frame-rate-capped present.

The 24bpp and 32bpp conversion paths disagree about R/B byte order, and anything other than 3 or
4 bytes per pixel leaves `temp_row` unfilled but still blits it. Currently unreachable because
non-32bpp server formats are rejected up front — a trap if that guard is ever relaxed.

## Watchdog

This app takes over the screen for long periods. The hardware watchdog is a 60 s timer; a
fullscreen app that stops feeding it gets the device hard-reset. If the app crashes without
cleanup, expect a reboot within ~60 s.

## Input

Touch maps to pointer events; USB keyboard maps through a keysym table; USB mouse gets 3-tier
acceleration (<3 px 1:1, 3–10 px 2×, >10 px 4×) — the same constants as `common/gamepad.c` and
the ScummVM backend, which is part of why C1 exists.

Devices are rescanned every 5 seconds for hotplug. Configuration is
`/etc/input_config.conf`, documented once in
[`../native_apps/README.md`](../native_apps/README.md#input-configuration).

Known gap: entering the exit zone mid-drag returns without sending a button-up, leaving the
remote mouse button stuck down.

## Settings GUI

`vnc_settings.c/h` is a touch settings screen with a full alphanumeric keypad; all fields are
editable. It writes the config with no `fchmod`, so the file lands 0644 with the password in
cleartext, and it renders the password in plain text on a wall-mounted panel while editing. Fix
both if you touch this code.

## 32-bit target

`sizeof(long) == 4`. `get_ticks_ms()` in `vnc_input.c` currently does `tv.tv_sec * 1000`, which
is signed overflow; it works only because callers use it as a `uint32_t` difference. Baseline to
a start timestamp captured at init instead.
