# probes/ — hardware measurement instruments

A shelf of one-off instruments for asking the silicon a question. **This is not a deployable
component and must not become one.** Nothing here is built, packaged or installed by any build
script, and `deploy-all.sh` cannot see this folder at all: it discovers components by globbing
`*/build-and-deploy.sh` (verified — `deploy-all.sh:71`), and there is deliberately no such script
here. Anything that grows into something a device should *keep* moves out into a component.

Every probe runs **on the device** and needs `/usr/local/bin/devmem_write`, which the `usb_host`
component builds and deploys. `scp` a probe over and run it there.

They were written for the 802.15.4 / XBee socket investigation, **closed 2026-09-02 by operator
decision** — not disproven; abandoned as hardware surgery on units that already have wired ethernet.
The modules have been removed. They are kept because the register sequences, the controls and the
measured failure modes transfer to any UART3, pinmux or GPIO question on this SoC. Socket pinout and
the measured 3.3 V rail: [Unpopulated and expansion](../HARDWARE.md#4-unpopulated-and-expansion).

| Script | What it measures | What it needs |
|---|---|---|
| `xbee_probe.sh` | Whether anything in the `J5` socket answers on UART3, from userspace with no DTB patch and no reboot: internal-loopback self-test first, then `AT` at all eight `BD` rates, then a framed `ATVR` in case of API mode, then the DOUT net's idle state. Restores the clock gate and the pad on exit. | A powered unit over ssh. Nothing opened, no meter, no reboot. |
| `xbee_socket_continuity.sh` | Whether `J5` pin 3 (`DIN`) and pin 2 (`DOUT`) reach the SoC's UART3 pads **at all** — each direction driven from the end that is reachable. | An **opened** unit with the **socket empty**, a DC voltmeter, a 330 Ω – 1 kΩ resistor, and an operator with hands on it. |
| `xbee_uart_break_semantics.sh` | What `LSR` `BI` actually means on this UART: four break/FIFO states under internal loopback, with `RXFIFO_LVL` and `DR` printed beside `BI` so a stuck flag can be told from a real one. A pure instrument check. | A powered unit over ssh. Touches no pad and no socket, so it is safe with a module fitted or absent. |
| `xbee_pad_gpio_read.sh` | Whether the UART3 RX ball can read LOW, asked through the **GPIO** module instead of the UART. Sweeps all six banks and identifies its own pin by which bit follows the pull. | A powered unit over ssh. Enables the `gpio2`–`gpio6` interface clocks and restores them on exit. |

⚠️ **`xbee_socket_continuity.sh`'s stepped mode leaves state behind on purpose, and only `restore`
puts it back.** The self-timed `run` mode prints a prompt and then sleeps 30 s, which is useless when
the script is driven over ssh from a host the operator is not watching — so `init`, `txlow`, `txidle`,
`phase2`, `phase2up`, `clearctl`, `padup` and `paddown` each **hold** their state until the next
invocation, at the operator's pace. Each one clears the `EXIT` trap, so the RX pad's pinmux and the
UART3 clock gate stay modified when it returns. **Always end a stepped session with
`./xbee_socket_continuity.sh restore`**, which puts the pad and the clock gate back and prints both
read-backs. An abandoned session leaves the unit muxed and clocked until it is power-cycled.

## The vendor ZigBee tooling — it no longer survives commissioning

⚠️ **`/opt/sbin` is deleted by default now.** It is a `vendorscripts` clean group in
`device-files/clean-rules.conf`, so both bring-up paths remove those ~1.4 MB of vendor shell scripts
unless you pass **`--keep-vendorscripts`**. That is the *only* way to keep the bytes: none of them are
in this repo and none of them may be — they are Steelcase's, and this project is published. If a probe
here ever needs the vendor's own implementation as a reference, commission with the flag, or read it
off a card backup. (`/opt/pv02` is a separate `keep` record in the same rules file and is unaffected.)

What that tooling was, so the loss is a decision rather than an accident — each fact has one home:

- the ZigBee gateway daemon, its channel mask and link key, the vendor's `AT`-command implementation
  and the burn-in script — [3.12 Serial ports](../SYSTEM_ANALYSIS.md#312-serial-ports)
- the network regenerator that rewrites all four network files every boot —
  [3.5 Network and power](../SYSTEM_ANALYSIS.md#35-network-and-power), and the operator-facing account in
  [The vendor network regenerator](../COMMISSIONING.md#the-vendor-network-regenerator)
- the cron-driven software watchdog and its repair/reboot chain —
  [3.13 Watchdogs](../SYSTEM_ANALYSIS.md#313-watchdogs)
- the backlight and LED scripts — [3.7 LEDs, backlight and PWM](../SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm)
- `ctrlblk`, the userspace half of the boot-tracker —
  [4.5 Control block and `boot_tracker`](../SYSTEM_ANALYSIS.md#45-control-block-and-boot_tracker)
- the upgrade machinery and its logger — [4.2 Partitions](../SYSTEM_ANALYSIS.md#42-partitions)
- the vendor stack all of it served — [5.1 As shipped](../SYSTEM_ANALYSIS.md#51-as-shipped)
