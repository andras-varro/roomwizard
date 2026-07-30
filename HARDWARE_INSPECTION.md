# Hardware Inspection Checklist

Physical checks that must be done on the unit with it **opened and powered off**. Everything in
this file is something that could not be determined over SSH — each one currently blocks or
de-risks a work item in [`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md).

Created 2026-07-29 from a read-only audit of RW09 (`192.168.50.73`).

**Updated 2026-07-30** — the unit (case label `RW29 1G-093`) was opened and photographed;
17 images are in [`HardwarePhotos/`](HardwarePhotos/). Every item below is now answered.
Findings that are *device facts* (panel, touch controller, PoE front end) have been copied into
[`SYSTEM_ANALYSIS.md`](SYSTEM_ANALYSIS.md) — this file keeps the inspection detail.

The teardown happened in two passes:

1. **Board in place** (`Top-*`, `Bottom-*`, `Screen-Controller`, `Touch-Connector-Probably`) — the
   PCB is wired on *both* faces, so it could not be flipped out without disconnecting the touch
   panel or the screen. This left §D and part of §H unanswerable.
2. **Full separation** (`bezel_with_touch_screen`, `display_screen`, `display_back_overview`,
   `display_controller_closeup`) — bezel, LCD module and board separated. This closed §D and §H.

> ### ⚠️ The touch flex broke during pass 2
>
> **The touch ribbon on this unit (`RW29 1G-093`) was torn while separating the bezel from the
> board, and the unit no longer has working touch.** Two more devices remain.
>
> **For the remaining units:** the touch glass is bonded to the *bezel*, but its flex tail runs
> down to `J2` on the *board*. Bezel and board are therefore tethered by a short, fragile
> 8-conductor flex that is easy to miss and easy to tear. **Release `J2` before separating the
> bezel** — do not lift the bezel and hope the flex has slack. It does not.
>
> **The broken unit is not scrap — it is now the right unit for the risky work.** `gamepad.c`
> already abstracts touch, USB keyboard, USB mouse and Xbox gamepad behind the same button IDs, so
> a touchless device is still a fully usable target over USB or VNC. That makes it the obvious
> candidate for soldering pins into `P4` and chasing the serial console (§A) — the one job that
> wants a unit you are willing to damage.

Photo naming: `Top-*` = the face toward the screen, `Bottom-*` = the face toward the rear cover
(this is the side carrying the SoC, RAM, NAND, Ethernet, PoE and all the candidate headers).

---

## Before you start

**Safety**

- Power off and unplug. The panel backlight runs at elevated voltage.
- **The unit is PoE-powered** (confirmed — section F). "Unplug" means pulling the **Ethernet**
  cable; there is no separate power lead. When powered, the RJ45 (`J3`) corner of the board carries
  up to **57 V** on the magnetics centre taps — keep probes away from it.
- The board is a TI OMAP3503 design — 3.3 V logic. **Do not connect 5 V TTL serial adapters.**
  **But note:** the candidate console header `P4` sits behind a MAX3232 and is at **RS-232**
  levels — a 3.3 V TTL adapter is the wrong tool *there*. See section A.
- Wear a wrist strap or at least discharge yourself on something grounded.
- Photograph every connector and ribbon before unplugging anything.
- **Do not try to flip the board out.** The touch flex (`J2`) and the panel harness land on the
  screen-facing side; freeing the board means disconnecting them.

**Bring**

- Torx/Phillips set for the case
- Multimeter (continuity + DC volts) — the only instrument any *open* item still needs
- Magnifier or phone macro lens for chip markings
- **PoE injector or PoE switch port** if the unit must be powered on the bench — there is no
  barrel jack, so this is the only way (§F). Not needed for continuity checks or photography.

*Only if §A is ever revived* (it is currently **declined** — see section A): an **RS-232** adapter
(USB↔DB9) for `P4`, or a 3.3 V USB-TTL adapter for tapping `U27`'s logic side, plus 2×5 / 2×7 0.1"
header pins to solder or pogo probes for the unpopulated holes.

**Record results** by editing the "Result" line under each item and committing. An unanswered
item is as valuable to record as an answered one — write "checked, not found" rather than
leaving it blank.

---

## A. UART / serial console — ~~highest priority~~ **DECLINED 2026-07-30**

> **Decision: not pursuing serial. The header was located; nobody is going to solder to it.**
>
> The original framing below — *"every failed experiment becomes a card-pull"* — assumed a
> card-pull was expensive. It isn't. The working recovery loop is: **pop the SD card, reimage it,
> set up DHCP, SSH back in.** Minutes, no soldering, no RS-232 adapter, no PoE injector on a bench.
>
> **This is not a gap, because the SD card is the entire failure surface.** Under the standing
> rules (never write `/dev/mtd*`; never overwrite `mlo` / `u-boot.bin` / `ctrlblock.bin`; leave
> `uImage-system` alone) U-Boot and NAND are never modified, and `bootcmd` is hardcoded — so a
> power cycle already undoes any bad kernel, and anything you *can* break lives on the removable
> card. Serial would buy **boot-time visibility**, not recovery capability.
>
> **Revisit only if** you ever decide to write NAND or replace U-Boot — at which point the card
> stops being the whole failure surface and serial becomes genuinely load-bearing. Until then this
> section is reference material: the header is identified, so picking it up later is cheap.

Original rationale, retained for context: this was the single un-de-risked link in the recovery
chain — without serial there is no boot visibility and no interactive fallback.

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

- [x] UART header found? **Result:** **Two unpopulated 0.1" (2.54 mm) through-hole headers, `P3`
      and `P4`**, on the SoC side (`Bottom-Top-Right.jpg`), in the top-right corner of the board
      between the two black socket strips `J5` and `J6`. Both have a **square pad marking pin 1**.
  - **`P4` = 2×5 (10 pins)** — sits **immediately beside `U27`**, which is a TI **MAX3232C**
    (TSSOP-16, marked `MA3232C / 7AK G4 / A1R7`). A dual RS-232 transceiver adjacent to a 2×5
    header is the classic IDC-to-DB9 debug-console arrangement. **This is the prime candidate.**
  - **`P3` = 2×7 (14 pins)** — a fan of ~8–10 parallel traces runs from it toward the SoC.
    2×7 / 0.1" is the **TI-14 JTAG** pinout. Likely JTAG, not UART.
  - Which header carries which function is *inferred from adjacency, not verified* — confirm with
    continuity from `U27` pins 7/8/9/10 (the RS-232 driver/receiver I/O) to `P4`.
- [x] **Pinout verified by continuity, 2026-07-30.** `P4` **is** the RS-232 console, on MAX3232
      channel 1. Only three pins are connected — the rest of `P4` is unwired.

      P4 pin 2  →  U27 pin 14 (T1OUT)  =  console TX   (RS-232 level, out of the device)
      P4 pin 3  →  U27 pin 13 (R1IN)   =  console RX   (RS-232 level, into the device)
      P4 pin 5  →  U27 pin 15 (GND)    =  ground

      Pin 1 is the square pad; even pins are the top row, odd pins the bottom row.
      **Three wires and no soldering strictly required** — a 0.1" female jumper or pogo pins in
      the plated holes is enough. Still RS-232 levels: a 3.3 V TTL adapter will not work here.
- [ ] Console output confirmed at 115200? **Result:** _not attempted, and not planned — see the
      decision box above. The hookup is now fully specified if anyone changes their mind._
- [ ] Can you interrupt to the `rw20 #` prompt? **Result:** _not attempted._

### `P3` is TI-14 JTAG (high confidence)

Continuity from `P3` to `U27` was measured at the same time, and the pattern is diagnostic:

| `P3` pin | Measured | TI-14 JTAG expects | |
| --- | --- | --- | --- |
| 4 | GND (via `U16` pin 53) | GND | ✔ |
| 5 | 3.3 V (`U27` pin 16, VCC) | `PD` / Vref — target power sense | ✔ |
| 6 | no connection | **keyed — no pin** | ✔ |
| 8 | GND (`U27` pin 15) | GND | ✔ |
| 10 | GND (`U27` pin 15) | GND | ✔ |
| 12 | no connection | GND | ✖ **one discrepancy** |

Four hits plus the key position is far past coincidence — a Vref on pin 5 with grounds on 4/8/10 is
the TI-14 signature and nothing else uses that arrangement. Pin 12 reading open is the one loose
end; it may be a no-connect on this board or simply a missed probe. The remaining pins (1, 2, 3, 7,
9, 11, 13, 14 — TMS, nTRST, TDI, TDO, RTCK, TCK, EMU0, EMU1) show nothing against `U27` because
they run to the SoC, which was not probed.

Not actionable — the boot-safety rules are built specifically so that JTAG is never needed — but
worth knowing it is there, and worth *not* mistaking it for a second serial port.

> ⚠️ **Correction to the "Before you start" advice for this header.** Because `P4` is wired
> through a MAX3232, its pins are almost certainly at **RS-232 levels (±5…±12 V)**, *not* 3.3 V
> TTL. Connecting a 3.3 V USB-TTL adapter directly to `P4` can damage the adapter and will not
> work. Two safe options:
> 1. Use a **real RS-232** adapter (USB↔DB9, or a USB-RS232 cable) on `P4`; or
> 2. Tap the **TTL side of `U27`** (the pins facing the SoC) with a 3.3 V adapter and leave `P4`
>    unconnected. Identify which side is which with a meter before powering on: the TTL side idles
>    at ~3.3 V on TX, the RS-232 side idles at a *negative* voltage on TX.
>
> Note also that the header is **unpopulated** — pins must be soldered in, or spring-loaded
> pogo/needle probes used against the plated through-holes.

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

- [x] Radio module present? **Result:** **Socket present, empty — on all three units.** `J5` and
      `J6` are two **populated 1×10 black female socket strips** on the **bottom** side of the
      board (`Bottom-Top-Right.jpg`), facing each other. `P3`/`P4` pass through the channel between
      them and are silkscreened on the **top** side — the two pairs share board area but are
      labelled on opposite faces, which is why they appear together in the bottom-side photo.
      **No radio is fitted in any of the three devices**, so this was not a per-unit option — the
      whole batch shipped without it.
- [x] **Mechanical fit verified with a real module, 2026-07-30.** A Digi XBee (≈10 years old,
      working condition unknown) was test-fitted: it **seats perfectly**. `J5` carries a white
      **pin-1 dot** that aligns with the module's pin 1, and the **metal inner bezel has a
      trapezoidal cut-out matching the XBee's outline**. The chassis was tooled for this module.
      That removes any remaining doubt about what the socket is for. *Not yet powered on with the
      module inserted.*
- [x] Part markings? **Result:** none — plain socket strips, no module to read. Nearby: `LED1`–`LED4`
      in a row alongside `J5`, and `U32` (16-pin TI, marked `WP245 / TI 81K / CD4S`, function
      unidentified — plausibly a level shifter between 3.3 V SoC I/O and the socket).
- [x] Antenna type? **Result:** **None on the main PCB** — no chip antenna, no PCB trace antenna,
      no U.FL or RP-SMA connector anywhere in the photos. Consistent with the XBee reading: on an
      XBee the antenna is part of the *module*, so an empty socket correctly has no antenna.
- [ ] If a socket/footprint exists, can you trace it to UART3 pins? **Result:** _not traced — needs
      a meter. Only the socket geometry has been measured so far._

**Measurement (this is what makes it an XBee socket rather than a generic header).** Pitch was
calibrated in-photo against `P3`, whose 0.1" / 2.54 mm spacing is known:

| Property | Measured | Digi XBee footprint |
| --- | --- | --- |
| Pin count | 10 per row, 2 rows (`J5` + `J6`) | 2 × 10 |
| Pitch | ≈ **2.0 mm** | **2.00 mm** |
| Row-to-row spacing | ≈ **24 mm** | **22.86 mm** (0.9") |

2 mm pitch is unusual — most through-hole headers on this board are 2.54 mm — and 2×10 at 2 mm
with ~23 mm row spacing is the XBee/XBee-PRO form factor specifically. Combined with the vendor
rootfs evidence above (`zbgatewayd /dev/ttyS2`, `pv02_app` issuing `ATID`/`ATCH`/`ATMY`/`ATDL`),
this is an **XBee socket that shipped without the paid radio option on this unit**.

**Consequence for plan item F5:** *do not* close it — the socket is real and wired. But F5 now has
a **hardware prerequisite**: source a Digi XBee (802.15.4, 2 mm pitch through-hole) module — and a
**second one**, since RoomWizard-to-RoomWizard needs a radio at each end. Before buying anything,
do the cheap verification: confirm continuity from `J5`/`J6` pins 2 and 3 (XBee DOUT / DIN) to the
SoC's UART3 balls or to the `serial@49020000` pads, and confirm 3.3 V on pin 1 / GND on pin 10
with the unit powered. If the socket turns out not to reach UART3, *then* close F5.

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

- [x] Second USB connector or footprint? **Result:** **Checked, not found.** One USB connector
      only: **`J4`, a micro-USB socket** on the top edge of the SoC side (`Top-Top-Middle.jpg`,
      `Connectors-reset-button.jpg`), aligned with a matching opening in the case. No USB-A
      footprint, no `USB`/`H1`/`H2` header, and no visible 4-pin unpopulated footprint anywhere on
      the observable face. Two unpopulated footprints *do* exist but are the wrong shape: **`P2`**
      (a long, fine-pitch 2-row connector — far too many pins for USB) and **`U33`** (a wide
      SOIC/SSOP IC land, not a connector).
- [ ] Which controller does the *existing* working port belong to? **Result:** _undetermined from
      photos._ The micro-USB form factor is itself weak evidence for **MUSB OTG** (micro-B is the
      OTG connector; EHCI host ports are normally USB-A), and that matches the existing working
      USB-host hack, which drives MUSB. Confirming means tracing `J4`'s D+/D− to the SoC, which
      needs the board out of the chassis — *(not observable in situ)*.

> **Reading for plan purposes:** the two EHCI ports in the device tree appear to be **SoC
> capability that Steelcase never brought to a connector on this board revision** (550-0204-03).
> Even if `CONFIG_USB_EHCI_HCD` could be enabled, there would be nothing to plug into. That makes
> the EHCI item informational-only for good, not merely kernel-blocked.

**Case openings.** The rear edge (`Connectors-reset-button.jpg`) has, in order: the **RJ45**
(`J3`), the **micro-USB** opening, a **second rectangular slot** (unidentified — no connector
behind it in the photos; possibly for a variant SKU), and a **pinhole** over a white round tact
**reset button**.

---

## D. Ambient light sensor

Software says it probably exists (the vendor factory test has a dedicated light-sensor test on
I2C bus 1) but it is not in the current device tree, and it was deliberately **not probed** —
the vendor's own script warns the test can hang the I2C bus, and bus 1 carries the PMIC.

**What you're looking for:** a small clear or tinted SMD package with a window, usually near the
front bezel edge or behind a small aperture in the enclosure. Common families: TSL2550, TSL256x,
ISL29003, APDS-9xxx.

**Record**

- [x] Sensor visible? **Result:** **No — closed.** The pass-2 photos separate the bezel from the
      LCD and show the **entire inner surface of the bezel** (`bezel_with_touch_screen.jpg`). It
      carries only: four metal screw bosses, moulded ribs, the bonded touch glass with its printed
      black mask, the two LED light-bar PCBs down the left and right edges, and the touch flex
      exit at the bottom. **No sensor package, no light pipe, no sensor daughterboard.**
- [x] Part markings? **Result:** n/a — nothing fitted to read.
- [x] Is there a light aperture in the bezel/case? **Result:** **No — and this is the decisive
      finding.** The bezel's front face is unbroken `>PC/ABS<` moulding (part `560-0540-0x`) with a
      single rectangular window for the display and no secondary aperture, pinhole or light pipe
      anywhere. The only pinhole on the whole enclosure is the **reset button** on the rear edge,
      which is opaque-backed and mechanical.

> **Plan item F3 (auto-backlight) — CLOSE IT.** The argument no longer depends on whether a sensor
> part is fitted: **the enclosure is light-tight.** With no aperture and no light pipe, an ambient
> light sensor would have nothing to sense even if one were populated somewhere on the board. The
> vendor's I2C-bus-1 light-sensor factory test is shared firmware for a product family in which
> this SKU is not the one with the sensor. There is nothing left to probe and nothing to build.
>
> *(Auto-dimming by time-of-day is still perfectly possible and needs no hardware — but that is a
> different, much smaller feature.)*

**Unblocks:** auto-backlight (plan item F3). If you find the part number, the I2C address can be
looked up rather than scanned for, which avoids the risky bus probe entirely.

---

## E. Audio — microphone and headphone jack

The TWL4030 codec registers a **capture PCM** and exposes full mic/line-in mixer routing, but the
vendor's `init_amixer.sh` never unmutes any mic — weak evidence that nothing is physically wired.
The codec also has a stereo `Headset` output path distinct from the `PreDriv` path that drives the
mono speaker.

**Record**

- [x] Microphone present (small can/SMD MEMS mic, or a hole in the case)? **Result:** **Checked,
      not found.** No MEMS mic package, no electret can, and no acoustic port anywhere in the case
      photos. This corroborates the software reading (`init_amixer.sh` never unmutes a mic).
- [x] 3.5 mm jack or unpopulated jack footprint? **Result:** **Checked, not found.** No jack, and
      no unpopulated jack footprint — a 3.5 mm barrel jack leaves a large, unmistakable land
      pattern and there is none. The `Headset` mixer path exists in the codec and goes nowhere.
- [x] Speaker: how many, and what size? **Result:** **One.** `SPKR1` — a single square metal-can
      magnetic speaker (`Bottom-Center.jpg` / `Bottom-Bottom-Left.jpg`), roughly 20 mm square,
      soldered directly to the board. **Mono output is a hardware fact, not a driver limitation**
      — matches the codec's `PreDriv` mono path.

**Unblocks:** ~~microphone-as-input games~~ (**closed — no mic hardware**); ~~stereo output via the
`Headset` path~~ (**closed — no second transducer and no jack**). Audio on this device is and stays
**mono, one small speaker**. The remaining audio headroom is software: the OSS quirks and the ~50 %
attenuation documented in the root `CLAUDE.md`.

---

## F. Power — is it PoE?

Invisible to software (`/sys/class/power_supply/` shows only `twl4030_ac`/`twl4030_usb`). If PoE
is present it is a passive splitter ahead of the barrel jack, or magnetics on the RJ45.

**Record — ANSWERED. This is a proper on-board 802.3af PD. There is no barrel jack at all.**

- [x] Barrel jack voltage (printed on the case or PSU)? **Result:** **No barrel jack exists.** The
      only power path into the unit is the **RJ45**. Confirmed by the owner: the device is run
      from PoE.
- [x] PoE magnetics / splitter board present? **Result:** **Yes — integrated, not a splitter.**
      The full PD front end is on the main PCB (`Bottom-Bottom-Right.jpg`, `Bottom-Overview.jpg`):

| Ref | Part | Role |
| --- | --- | --- |
| `J3` | TE **MagJack 1-6605834-1** | RJ45 with integrated magnetics + centre taps |
| `U1` | TI **TPS23750** (`6BTG4 / A89N`) | 802.3af PD interface **+** integrated DC/DC controller |
| `T?` | Coilcraft **POE13F-12L** (`1809 J`) | isolated flyback transformer — the POE13F series is Coilcraft's TPS23750 reference part |
| `Q1` | `4848 5BD` | flyback switching FET |
| `D1` | TO-252, `NHSTQW 3406` | secondary rectifier |
| `U2` | `MT1107 V74968` | isolated feedback / opto-side regulator |
| `U4` | `PSS4325 / 68154 / C935` | TPS54325-class step-down (secondary rail) |

**Implications worth knowing:**

- **No wall adapter is needed or possible.** Bench work requires a PoE injector or a PoE switch
  port. Budget for it before planning any out-of-case session.
- 802.3af class budget is **12.95 W at the PD**, and the TPS23750 enforces the class signature.
  Anything added inside the case (an XBee at ~50 mA, a USB device on `J4`) draws from that budget.
  A bus-powered USB hard drive is not going to work.
- **The RJ45 is isolated** (magnetics in `J3`, transformer isolation in the flyback), so the
  Ethernet side is safe. But **`J3`'s centre taps carry up to 57 V** — that corner of the board is
  the one place to keep probes away from while powered.
- A small white **`C&K CHINA(9) EP11 0.4VA MAX 1744`** transformer sits near the touch flex,
  separate from the PoE section; its purpose is unconfirmed.

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

- [x] Any unpopulated headers / test-point arrays on the board? **Result:** **Yes, plenty — this
      board is unusually generous with test points.**
  - **Unpopulated headers:** `P3` (2×7, 0.1" — likely JTAG), `P4` (2×5, 0.1" — likely RS-232
    console), `P2` (long fine-pitch 2-row connector, purpose unknown — the most interesting
    unknown on the board).
  - **Empty sockets:** `J5` + `J6` (the 2×10 / 2 mm XBee socket, section B) — already wired,
    already has pins, easiest place to attach anything serial.
  - **Test points:** numbered `TP1`–`TP59`, scattered over the whole SoC side. Dense clusters:
    **`TP19`–`TP31`** around the SD slot, **`TP13`–`TP18`** mid-board, and **`TP34`–`TP36`** plus
    a long run **`TP39`–`TP59`**. Individually labelled and probe-sized.
  - **Unpopulated IC land:** `U33`, a wide SOIC/SSOP footprint.
- [ ] Can you identify pads for any TWL4030 GPIO or ADCIN pin? **Result:** **Not from photos.** The
      test points are numbered, not named — silkscreen `TP47` says nothing about what net it is on.
      Mapping them needs continuity probing back to `U14` (the TPS65930/TWL4030) with the board
      accessible, and `U14`'s ADCIN pins are BGA-hidden. **The practical route is the reverse:**
      power the unit, read `/sys/bus/iio/.../in_voltage*_raw` for `ADCIN2..7` while touching each
      candidate TP with a resistor to 3.3 V, and see which channel moves. That is a software-side
      experiment and needs no teardown.
- [x] Is there physical room inside the case for added components? **Result:** **Yes, modestly.**
      There is clear open board area around the top-right corner (the `J5`/`J6`/`P3`/`P4` region)
      and near the unpopulated `U33` and `P2` lands. An XBee dropped into `J5`/`J6` sits in the
      space it was designed for. Note the two power constraints, though: the 802.3af budget
      (section F) and the fact that the case has **no ventilation slots** visible.

---

## H. General documentation pass

While the case is open, capture things that are cheap now and expensive later:

- [x] **Photograph both sides of the PCB at high resolution.** **Done — 14 images in
      [`HardwarePhotos/`](HardwarePhotos/)**, covering both faces at overview and detail level.
      Caveat: the screen-facing side is photographed *in place*, so the region behind the panel is
      not covered (see the standing constraint at the top of this file).
- [x] Note all major chip markings. **Done — see the table below.** The software-side guesses were
      right about NAND and the Ethernet MAC, and **wrong about the PHY**: there is no separate
      LAN8700. `LAN9221` is a MAC+PHY in one package, so the PHY is inside it.
- [x] Note the board revision / part number silkscreen. **Result:** Steelcase Inc **`550-0204-03`**,
      **© 2010**, `STM-5 STM-5E20784`, `94V-0`, `TESTED Compulrol`. Asset labels `46837.0300` and
      `47270.0310`; case label `RW29 1G-093`.
- [x] Confirm the **SD card is removable without desoldering** and note its size/type. **Result:**
      **Yes — and this is the primary recovery path, so the detail matters.** `J1` is a **microSD
      push-push socket** (corrected: an earlier photo reading called it full-size), not soldered
      eMMC, accessible from the top side. Card fitted: **SanDisk "Video Buffer" 4 GB, SDHC, UHS-I,
      speed class U3.** "Video Buffer" is SanDisk's OEM **high-endurance** line, sold for dashcams
      and surveillance recorders — a sensible choice for a device that gets hard power-cycled, and
      worth matching when replacing or cloning. U3 guarantees ≥30 MB/s sustained write.

> **This card is the whole recovery story.** Pull it, reimage it, put it back — see the §A decision
> box. That is also why the serial console was declined. Keep a known-good image, and prefer a
> high-endurance card when cloning to a larger one (`clone-to-32gb.sh`, `SD_CARD_UPGRADE.md`).
- [x] Note whether the case can be reopened non-destructively (clips vs. glue vs. screws).
      **Result:** **Yes — screws and clips, no glue.** `display_screen.jpg` shows the loose
      **self-tapping screws** from the LCD mount; `bezel_with_touch_screen.jpg` shows four **metal
      threaded bosses** moulded into the bezel plus plastic retention clips along the top and
      bottom edges. Nothing is bonded except the touch glass to the bezel (which is meant to be
      permanent). The enclosure is fully serviceable and re-closable.
      **The one destructive risk is the touch flex, not the case** — see the warning at the top.

### Parts inventory (from the photos)

| Ref | Marking | Identification |
| --- | --- | --- |
| `U11` | `OMAP3503ECUS`, `72P19HQ`, `G1` | TI **OMAP3503** application processor ✔ matches software |
| `U12` | `D9RMJ`, `70CI7 / XQ52` | Micron **mobile DDR SDRAM** (D9RMJ is Micron's FBGA code) |
| `U13` | `NQH53`, `7ME12 / X5Y3` | Micron **NAND flash** — consistent with the expected MT29F2G16ABBEAHC ✔ |
| `U14` | `TPS65930A2`, `74AJKFW $4`, `G1` | TI **TPS65930** = PMIC + audio codec, **TWL4030 family** ✔ |
| `U15` | `LAN9221-ABZJ`, `A1751-AB24 / 751K44A / ASE-TW` | SMSC **LAN9221** Ethernet **MAC+PHY** on the GPMC bus ✔ |
| `U16` | `LVDS83B`, `77AK23K G4` | TI **SN65LVDS83B** — parallel-RGB → **LVDS** transmitter to the panel |
| `U27` | `MA3232C`, `7AK G4`, `A1R7` | TI **MAX3232C** dual **RS-232** transceiver (see section A) |
| `U1` | `TPS23750`, `6BTG4 / A89N` | TI **802.3af PoE PD** + DC/DC controller (see section F) |
| `U4` | `PSS4325 / 68154 / C935` | **TPS54325**-class synchronous buck |
| `U17` | `⧠M` logo, `GC5.5V0.47F`, `JAPAN` | **Supercapacitor, not a battery** — Panasonic/Matsushita **"Gold Cap"** GC series, 5.5 V 0.47 F. RTC hold-up. See note below. |
| `U22`, `U23` | — | Drivers for the side LED bars (`L8`/`L2` inductors adjacent) |
| `U24` | `CCH / TI 8A / Z86W` | TI, **unidentified** |
| `U25`, `U32` | `WP245 / TI 81K / CD4S` (16-pin, both) | TI, **unidentified**. `U25` sits beside the touch flex; `U32` beside `J5` + `LED1`–`LED4`. Position suggests level shifters/buffers. |
| `U33` | — | **Unpopulated** wide SOIC/SSOP land |
| `Q1`, `D1`, `U2` | `4848 5BD` · `NHSTQW 3406` · `MT1107 V74968` | PoE flyback FET / rectifier / feedback |
| `SPKR1` | — | Single square metal-can magnetic speaker (section E) |
| `LED1`–`LED4`, `LED5` | — | Discrete SMD LEDs; `LED1`–`4` in a row by `J5` |

### Connectors

| Ref | Type | Goes to |
| --- | --- | --- |
| `J1` | **microSD** push-push socket | Boot/root storage (card installed) |
| `J2` | 8-pin white **FFC** | **Touch** controller flex |
| `J3` | TE **MagJack 1-6605834-1** RJ45 | Ethernet **+ PoE power in** |
| `J4` | **micro-USB** | The working USB port (likely MUSB OTG — section C) |
| `J5`, `J6` | 1×10 sockets, 2 mm pitch, **empty** | **XBee** radio site (section B) |
| `J7`, `J8` | 5-pin white **JST** | Harnesses to the left/right case edges — the **side LED status bars** |
| `P2` | Long fine-pitch 2-row, **unpopulated** | Unknown |
| `P3` | 2×7, 0.1", **unpopulated** | Likely **JTAG** (section A) |
| `P4` | 2×5, 0.1", **unpopulated** | Likely **RS-232 console** via `U27` (section A) |
| — | 40-pin **FFC** | Display panel harness |

### Display and touch — new part numbers

Neither of these was known before the teardown; both are now also recorded in
[`SYSTEM_ANALYSIS.md`](SYSTEM_ANALYSIS.md).

**Panel: Sharp `LQ070Y3LG4A`** — 7", WVGA 800×480, LVDS. This **confirms** the panel-driver name
already inferred from omapdss (`sharp,lq070y3lg4a`) — the compatible string in the vendor DTS is
the literal part number. Module labels (`display_back_overview.jpg`):

| Label | Reading |
| --- | --- |
| `LQ070Y3LG4A   84000427A` | Sharp part number + Sharp's own serial |
| `KLMPK0857TPZA W180322 00391 0` | module/backlight code; `W180322` = **2018-03-22** |

Its T-CON (`display_controller_closeup.jpg`) is a Sharp board silkscreened **`K5784TP`**
(`LF a / Sn-Ag-Cu` lead-free, `52-994V-0 E222034`, date code **`1810`** = 2018 week 10), carrying:

- **`60153F00B0 / G18050152 / JAPAN`** — Sharp timing controller, ~100-pin QFP
- **`A770 / 7X 02 / JRC`** — New Japan Radio, QFN — gamma buffer / reference amplifier
- one **unpopulated QFN land** at the right edge
- a small **white 5-pin JST** (top-left) — backlight feed
- a **JAE ~30-position** connector at the bottom edge, of which only ~14–16 positions are wired,
  feeding the discrete white twisted-pair harness to the main board's 40-pin FFC. That count is
  right for LVDS: four data pairs + a clock pair + power and grounds.

> **The unit was built in ~March 2018, on a board designed in 2010.** *(Corrected — an earlier
> reading of this called the panel a service replacement, on the strength of the panel date alone.
> A third date code settles it the other way.)* Three independent 2018 date codes cluster within
> two weeks of each other:
>
> | Part | Marking | Date |
> | --- | --- | --- |
> | LCD module | `W180322` | 2018-03-22 |
> | T-CON board | `1810` | 2018, week 10 |
> | `J3` MagJack | `18111` | 2018, week 11 |
>
> A panel swap would not also replace the Ethernet jack. So **`© 2010` on the silkscreen is the
> board's design copyright, not its build date** — Steelcase was still shipping this design eight
> years later. (The Cypress touch chip's `1043` code is then long-lived module stock, which is
> ordinary for a bonded touch assembly.) Practical upshot is unchanged and still good: **the panel
> is a separate, replaceable module and Sharp was producing `LQ070Y3LG4A` in 2018** — a cracked
> screen is a repair, not a write-off.

**Bezel:** `>PC/ABS<`, moulded part number `560-0540-0x`. The touch glass is **bonded to the bezel**
(not to the LCD), and the two **side LED status bars** are separate green PCB strips clipped down
the left and right inner edges, each with a 5-pin white JST harness — **confirming `J7`/`J8`** on
the main board as the side-LED connectors.

**Touch controller: Cypress `CY8CTMG120-56LTXI`** — on an orange flex marked
**`EDT REV.A 40-0016-2`**, `IC1`, full marking `CY8CTMG120-56 / LTXI 1043 / A 05 CHI / CYP 654793`
(56-QFN, date code week 43 2010). This is a **PSoC TrueTouch Multi-Touch Gesture** controller.

> **This corrects a long-standing assumption.** The kernel driver is called `panjit_ts` and the
> docs have treated "Panjit" as the controller vendor. **Panjit is the touch-module/sensor vendor;
> the silicon is Cypress.** That matters because it makes the multi-touch path in
> `SYSTEM_ANALYSIS.md#4-multi-touch-via-direct-i2c` considerably more tractable: the
> CY8CTMG120's I2C register map is public Cypress TrueTouch documentation, rather than an
> undocumented Panjit protocol that would have to be reverse-engineered from bus captures. Two-point
> touch and on-chip gestures over `/dev/i2c-2` (address `0x03`) is a **userspace** job — **no kernel
> work, so it is not blocked by the kernel policy.**

---

## Summary table

Status as of the 2026-07-30 photo inspection.

| # | Check | Priority | Status | Outcome |
|---|-------|----------|--------|---------|
| A | UART console header | ~~Critical~~ **Declined** | **Located, not pursued** | `P4` (2×5) by `U27` MAX3232 — **RS-232, not TTL**. SD-pull + DHCP + SSH is the recovery loop instead. |
| B | ZigBee radio populated? | High | **Answered** | Empty **2×10 / 2 mm XBee socket** (`J5`+`J6`). F5 stays open, now needs 2 modules. |
| D | Ambient light sensor | Medium | **Answered — negative** | No sensor **and no aperture**: the enclosure is light-tight. **F3 is closed.** |
| H | PCB photos + markings | Medium | **Done** | Full parts inventory; panel + touch part numbers; case is screws + clips, reopenable. |
| C | Second USB port | Low | **Answered** | One micro-USB (`J4`), no second port, no footprint. EHCI has nowhere to go. |
| E | Mic / headphone jack | Low | **Answered** | No mic, no jack, one mono speaker. Both audio ideas closed. |
| G | Expansion pads | Low | **Partly** | `TP1`–`TP59` + `P2`/`P3`/`P4`/`U33` unpopulated; nets unmapped. |
| F | PoE | Low | **Answered** | On-board 802.3af PD (TPS23750 + Coilcraft POE13F + MagJack). No barrel jack. |

### What is still open, and what it costs

1. **Bare-board photos of the dissected unit.** The board is now free of both the bezel and the
   LCD, so for the first time **both faces can be shot unobstructed**. This is the cheapest
   remaining item and the one that unlocks the others: it would settle `P2`, `U33`, and let the
   `TP1`–`TP59` array be mapped from a picture instead of a meter. Zero risk — the unit is already
   apart and has no touch left to lose.
2. **Is the XBee populated on the other two units?** Different SKUs, possibly different options.
   **This is low-risk on an intact unit:** `J5`/`J6` are on the *rear-facing* side, so **removing
   only the rear cover shows them — you never separate the bezel**, which is the step that kills
   the touch flex. A glance answers it.
3. **Verify `J5`/`J6` really is on UART3** before buying XBee modules. Meter continuity from
   `J5`/`J6` pins 2/3 to the UART3 pads, ~10 minutes on the dissected board.
4. **Map a few test points to TWL4030 ADCIN channels** — mostly software: power a unit, touch a
   resistor from 3.3 V to a candidate `TP`, watch which `in_voltage2..7_raw` moves. Unlocks F4.
5. ~~**Consumables:** RTC cell type and SD card class.~~ **Answered 2026-07-30 — see below.**

### RTC hold-up is a supercapacitor, not a battery

`U17` reads `GC5.5V0.47F` / `JAPAN` under an `M`-in-rounded-square logo — a **Panasonic
(Matsushita) "Gold Cap" 5.5 V 0.47 F supercapacitor**. An earlier note in this file called it a
coin-cell holder with a blue Japanese cell; that was a misread of the photo. Consequences:

- **Nothing to replace on a schedule, and no leakage risk** — there is no cell chemistry to
  exhaust. Supercaps do age (ESR climbs, capacitance falls) but over decades, not years. One less
  thing to worry about on hardware this old.
- **Hold-up is short.** 0.47 F carries the TWL4030 RTC for hours-to-days once self-discharge is
  counted, not the months a coin cell would. **Expect the clock to be wrong after any extended
  unplugged period.** That is why `setup-device.sh` does time-sync, and it means anything that
  trusts the RTC across a shelf-storage gap needs a sanity check rather than blind faith.
- `in_voltage9` reading 3184 mV means the cap is **charged**, not that a battery is healthy.

Everything else on the checklist is answered, and **§A is declined** — see the decision box in
section A. Items 1–3 need no power at all, so no PoE injector is required for any of them.

### Bezel obscuration — measured on-device, not from these photos

**Result: ~10–15 px on the top and bottom edges only; effectively nothing on the left or right.**
Verified on **two** physical devices (2026-07-30). Visible area is therefore about **800×455**.

This settles a contradiction the photos could not: `SYSTEM_ANALYSIS.md` had claimed *"~720×420,
bezel obscures ~30–40 px on all edges"* — wrong on both the magnitude and which edges — while the
root `CLAUDE.md`'s *"~10 px top and bottom"* was right. `SYSTEM_ANALYSIS.md` has been corrected.

The teardown photos genuinely could not answer this (bezel and LCD were never shot assembled and
square-on, so perspective error would have exceeded the quantity being measured) — worth
remembering as a case where the right move was to measure on hardware rather than squint at a JPEG.

**Consequence for layout:** the four `SCREEN_SAFE_MARGIN_*_DEFAULT` values of `0` are nearly right;
only the top/bottom 10–15 px are actually hidden. **The constraint that really binds interactive
layout is digitizer reach, not the bezel** — touch is not reported in the outer ~10 px left/right,
~26 px top, ~35 px bottom. Keep touch targets inside *that*; decoration can go to the edge.
