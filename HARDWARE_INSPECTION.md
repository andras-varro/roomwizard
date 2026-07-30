# Hardware Inspection Checklist

Physical checks that must be done on the unit with it **opened and powered off**. Everything in
this file is something that could not be determined over SSH — each one currently blocks or
de-risks a work item in [`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md).

Created 2026-07-29 from a read-only audit of RW09 (`192.168.50.73`).

---

## Before you start

**Safety**

- Power off and unplug. The panel backlight runs at elevated voltage.
- The board is a TI OMAP3503 design — 3.3 V logic. **Do not connect 5 V TTL serial adapters.**
- Wear a wrist strap or at least discharge yourself on something grounded.
- Photograph every connector and ribbon before unplugging anything.

**Bring**

- 3.3 V USB-TTL serial adapter (CP2102 / FT232 / CH340 with a 3.3 V jumper)
- Multimeter (continuity + DC volts)
- Magnifier or phone macro lens for chip markings
- Torx/Phillips set for the case

**Record results** by editing the "Result" line under each item and committing. An unanswered
item is as valuable to record as an answered one — write "checked, not found" rather than
leaving it blank.

---

## A. UART / serial console — **highest priority**

This is the single un-de-risked link in the whole recovery chain. Without serial you have no
boot visibility and no interactive fallback; every failed experiment becomes a card-pull.

**What you're looking for:** 3 or 4 pads/pins carrying GND, TX, RX (and possibly 3.3 V), most
likely near the SoC, near the SD slot, or along a board edge. Often an unpopulated 0.1"
(2.54 mm) header footprint, sometimes 2 mm pitch, sometimes just bare test pads.

**Known from software** (see `SYSTEM_ANALYSIS.md`):

- Linux `ttyO1` = the SoC's physical **UART2** at MMIO `0x4806c000`
- **115200 8N1, 3.3 V TTL**
- A root login shell is already running on it (`/etc/inittab`: `O1:12345:respawn:/bin/start_getty 115200 ttyO1 vt102`, and `ttyO1` is in `/etc/securetty`)
- U-Boot prints there too, with `bootdelay=1` — a 1-second window to reach the `rw20 #` prompt

> ⚠️ OMAP3 **UART1** is at `0x4806a000` and is `status = "disabled"` in the device tree.
> If you find two candidate headers, the live one is UART2. Probing the wrong pads wastes time
> but does no harm.

**Procedure**

1. Locate candidate pads. Silkscreen may say `J*`, `CON*`, `DBG`, `UART`, `TTL` or nothing at all.
2. Identify GND with a multimeter (continuity to the case/shield or to a large ground pour).
3. Connect **GND↔GND, adapter RX ↔ board TX, adapter TX ↔ board RX**. Leave the adapter's
   VCC disconnected — the board powers itself.
4. Open a terminal at **115200 8N1** (`picocom -b 115200 /dev/ttyUSB0`, or PuTTY on Windows).
5. Power on. You should see U-Boot output within a second or two.
6. If you see nothing, swap TX/RX and retry. If you see garbage, the baud is wrong or you're
   on 5 V — stop and recheck.

**Record**

- [ ] UART header found? **Result:** _(location / silkscreen / pin order / pitch — add a photo)_
- [ ] Console output confirmed at 115200? **Result:** _____
- [ ] Can you interrupt to the `rw20 #` prompt? **Result:** _____

**Unblocks:** safe kernel/DTB experimentation, boot diagnostics, recovery without pulling the card.

---

## B. Is the 802.15.4 / ZigBee radio populated?

Potentially the most interesting capability on the board — a wireless link between RoomWizards
with no network involved. But it may have been a paid SKU option, and under the current kernel
the port is dark (`serial@49020000` is `status = "disabled"`), so software cannot answer this.

**What you're looking for**, near a board edge:

- A small castellated surface-mount radio module (Digi XBee, or a similar 802.15.4 part), **or**
- A 2×10 pin **XBee socket** (two rows of 10, 2 mm pitch — distinctive), populated or empty, **or**
- An **unpopulated footprint** for either of the above
- Accompanied by a **chip antenna, PCB trace antenna, or U.FL/RP-SMA connector**

**Evidence it should exist** (from the vendor rootfs in `partitions/`):

```
opt/sbin/RoomWizard-zbgatewayd/readme.txt:
  ./zbgatewayd /dev/ttyS2 --baud 57600 ...
opt/pv02/pv02_app:  ATID / ATCH / ATMY / ATDL  XBee AT commands on /dev/ttyS2
conc_xbeespam.sh:   runs `pv02_app 8 spam` as a burn-in test
opt/sbin/wpantools_roomwizard  (172 KB ARM ELF)
```

Legacy `/dev/ttyS2` = OMAP **UART3** = `serial@49020000`.

**Record**

- [ ] Radio module present? **Result:** _(module present / socket empty / footprint unpopulated / nothing at all)_
- [ ] Part markings? **Result:** _____
- [ ] Antenna type? **Result:** _____
- [ ] If a socket/footprint exists, can you trace it to UART3 pins? **Result:** _____

**Unblocks:** the DTB patch to enable UART3 (plan item F5). **If nothing is populated, close F5
and delete it from the plan** — that saves real effort.

---

## C. USB — is there a second port or an unpopulated footprint?

The device tree declares **two EHCI high-speed host ports**, each with its own PHY and VBUS
regulator, and port 2 has a board-specific reset GPIO (`gpio1[13]`) — meaning Steelcase wired it
deliberately. But `CONFIG_USB_EHCI_HCD` is unset in the kernel, and without vendor source it
cannot be enabled.

This check is therefore **informational only** right now — but it determines whether the item is
worth ever revisiting, and it tells you whether the existing MUSB-OTG hack was working around the
wrong port all along.

**What you're looking for:**

- An unpopulated **USB-A footprint** on the PCB
- A 4-pin header labelled `USB`, `H1`, `H2` or similar
- Test pads near the `hsusb1_vbus` / `hsusb2_vbus` regulators
- How the existing (working) USB connector is wired — is it the MUSB OTG port or one of the EHCI ports?

**Record**

- [ ] Second USB connector or footprint? **Result:** _____
- [ ] Which controller does the *existing* working port belong to? **Result:** _____

---

## D. Ambient light sensor

Software says it probably exists (the vendor factory test has a dedicated light-sensor test on
I2C bus 1) but it is not in the current device tree, and it was deliberately **not probed** —
the vendor's own script warns the test can hang the I2C bus, and bus 1 carries the PMIC.

**What you're looking for:** a small clear or tinted SMD package with a window, usually near the
front bezel edge or behind a small aperture in the enclosure. Common families: TSL2550, TSL256x,
ISL29003, APDS-9xxx.

**Record**

- [ ] Sensor visible? **Result:** _____
- [ ] Part markings? **Result:** _____
- [ ] Is there a light aperture in the bezel/case? **Result:** _____

**Unblocks:** auto-backlight (plan item F3). If you find the part number, the I2C address can be
looked up rather than scanned for, which avoids the risky bus probe entirely.

---

## E. Audio — microphone and headphone jack

The TWL4030 codec registers a **capture PCM** and exposes full mic/line-in mixer routing, but the
vendor's `init_amixer.sh` never unmutes any mic — weak evidence that nothing is physically wired.
The codec also has a stereo `Headset` output path distinct from the `PreDriv` path that drives the
mono speaker.

**Record**

- [ ] Microphone present (small can/SMD MEMS mic, or a hole in the case)? **Result:** _____
- [ ] 3.5 mm jack or unpopulated jack footprint? **Result:** _____
- [ ] Speaker: how many, and what size? **Result:** _____

**Unblocks:** microphone-as-input games; possible stereo output via the `Headset` mixer path.

---

## F. Power — is it PoE?

Invisible to software (`/sys/class/power_supply/` shows only `twl4030_ac`/`twl4030_usb`). If PoE
is present it is a passive splitter ahead of the barrel jack, or magnetics on the RJ45.

**Record**

- [ ] Barrel jack voltage (printed on the case or PSU)? **Result:** _____
- [ ] PoE magnetics / splitter board present? **Result:** _____

---

## G. Expansion opportunities — where can wires actually go?

The GPIO map in `SYSTEM_ANALYSIS.md` lists many *unclaimed* GPIOs, but unclaimed ≠ reachable:
the pinmux only configures ten function groups, so most SoC balls are at ROM default and may not
reach a pad. **The 18 TWL4030 GPIOs are the safer expansion target** — they are guaranteed real
chip pins.

Similarly, the TWL4030 MADC has six general-purpose analogue inputs (`ADCIN2..ADCIN7`) sitting
idle at ~0 V. If any of them reach a pad or test point, that is a trivial path to real analogue
input (a potentiometer paddle for Pong, a light-dependent resistor, a slide fader).

**Record**

- [ ] Any unpopulated headers / test-point arrays on the board? **Result:** _(photograph and note silkscreen labels)_
- [ ] Can you identify pads for any TWL4030 GPIO or ADCIN pin? **Result:** _____
- [ ] Is there physical room inside the case for added components? **Result:** _____

---

## H. General documentation pass

While the case is open, capture things that are cheap now and expensive later:

- [ ] **Photograph both sides of the PCB at high resolution.** This alone answers most future
      questions without reopening the unit.
- [ ] Note all major chip markings: SoC, PMIC (TWL4030), RAM, NAND (expected: Micron
      MT29F2G16ABBEAHC), Ethernet PHY (expected: SMSC LAN8700 + LAN9221-class MAC on GPMC).
- [ ] Note the board revision / part number silkscreen.
- [ ] Confirm the **SD card is removable without desoldering** and note its size/type.
      (Recovery depends on this — see `SYSTEM_ANALYSIS.md`.)
- [ ] Note whether the case can be reopened non-destructively (clips vs. glue vs. screws).

---

## Summary table

| # | Check | Priority | Blocks |
|---|-------|----------|--------|
| A | UART console header | **Critical** | All kernel/DTB work; recovery |
| B | ZigBee radio populated? | High | F5 (RoomWizard-to-RoomWizard multiplayer) |
| D | Ambient light sensor | Medium | F3 (auto-backlight) |
| H | PCB photos + markings | Medium | Everything future |
| C | Second USB port | Low | Informational (EHCI is kernel-blocked) |
| E | Mic / headphone jack | Low | Optional audio features |
| G | Expansion pads | Low | Analogue input experiments |
| F | PoE | Low | Informational |

**Do A and H in the same session** — if you only open the unit once, those two are the ones that
pay off regardless of what else you decide to build.
