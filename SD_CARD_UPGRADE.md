# SD Card Upgrade: 4 GB → 32 GB

Upgrading the RoomWizard SD card from the stock 4 GB to a 32 GB SDHC card to
accommodate large ScummVM game data (Full Throttle, The Dig, Grim Fandango, etc.).

---

## Table of Contents

1. [Overview](#1-overview)
2. [Current Partition Layout (Stock 4 GB Card)](#2-current-partition-layout-stock-4-gb-card)
3. [Prerequisites](#3-prerequisites)
4. [Strategy — Recommended Approach](#4-strategy--recommended-approach)
5. [Step-by-Step Procedure (Recommended: Expand rootfs)](#5-step-by-step-procedure-recommended-expand-rootfs)
6. [Alternative: Add Separate Game Partition (p8)](#6-alternative-add-separate-game-partition-p8)
7. [New Partition Layout (After Upgrade)](#7-new-partition-layout-after-upgrade)
8. [Script Reference](#8-script-reference)
9. [Troubleshooting](#9-troubleshooting)
10. [ScummVM Game Storage](#10-scummvm-game-storage)

---

## 1. Overview

### Why Upgrade

The stock RoomWizard SD card is a ~3.7 GB (nominal 4 GB) card. The root
filesystem (p6) is only **~981 MB**, of which ~463 MB is used out of the box
(~474 MB free). Even after removing ~178 MB of Steelcase bloatware with
`setup-device.sh --remove`, only ~652 MB is free.

ScummVM games are deployed to `/opt/roomwizard/` on the rootfs. Large adventure
games require significant space:

| Game | Approximate Size |
|------|-----------------|
| The Dig | ~500 MB |
| Sam & Max Hit the Road | ~300 MB |
| Monkey Island SE | ~1.5 GB |
| Classic LucasArts (SCUMM) | 5–50 MB each |

With a 32 GB card and an expanded rootfs, there is ~27 GB of usable space —
enough for a large library of both classic and remastered titles.

### What Changes

- The **extended partition (p4)** is expanded to fill the entire 32 GB card.
- The **rootfs partition (p6)** is expanded from ~981 MB to ~27+ GB.
- The **swap partition (p7)** is **dropped** — not needed for gaming, and
  removing it lets p6 grow larger.
- The ext3 filesystem on p6 is resized in-place (no reformat).

### What Stays the Same

- **Boot partition (p1)** — FAT32, ~70.6 MB, bootable, MLO / U-Boot / uImage / DTB — untouched.
- **Data partition (p2)** — ext3, ~251 MB — untouched.
- **Log partition (p3)** — ext3, ~243 MB — untouched.
- **Backup partition (p5)** — ext3, ~1.40 GB — kept at original size for safety.
- **Kernel, U-Boot, device tree** — unchanged.
- **Commissioning workflow** — `commission-roomwizard.sh` → `setup-device.sh` → `deploy-all.sh` works identically because the rootfs UUID is preserved.

---

## 2. Current Partition Layout (Stock 4 GB Card)

The stock RoomWizard SD card has **7 partitions** (including an extended container
and a swap partition). This layout was captured from a real device using `sfdisk -d`:

| Partition | Type ID | Flags | Start Sector | Size (sectors) | Size (human) | Filesystem | Purpose |
|-----------|---------|-------|-------------|---------------|-------------|------------|---------|
| p1 | 0x0c | **bootable** | 63 | 144,522 | ~70.6 MB | FAT32 | Boot — MLO, U-Boot, uImage, DTB |
| p2 | 0x83 | | 144,585 | 514,080 | ~251 MB | ext3 | Application data |
| p3 | 0x83 | | 658,665 | 498,015 | ~243 MB | ext3 | System logs |
| p4 | 0x05 | | 1,156,680 | 6,586,650 | ~3.14 GB | — | Extended container (holds p5–p7) |
| p5 | 0x83 | | 1,156,743 | 2,939,832 | ~1.40 GB | ext3 | OEM backup |
| p6 | 0x83 | | 4,096,638 | 2,008,062 | ~981 MB | ext3 | Root filesystem (/) |
| p7 | 0x82 | | 6,104,763 | 530,082 | ~259 MB | swap | Swap space |

**Notable characteristics:**
- Partitions use **old CHS alignment** (starting at sector 63, not modern 2048-sector alignment)
- Label ID is `0x00000000`
- p1 has the **boot flag** set
- p7 is **swap** (type 0x82) — this is dropped during the upgrade to maximize rootfs space

---

## 3. Prerequisites

### Hardware

| Item | Notes |
|------|-------|
| Original 4 GB SD card | Working stock RoomWizard image |
| New 32 GB SD card | SDHC, Class 10 / UHS-I or better |
| Linux PC with SD card reader | Or WSL2 with USB passthrough (`usbipd`) |

### Software

All tools are standard on Debian / Ubuntu. Install if missing:

```bash
sudo apt-get install -y util-linux e2fsprogs coreutils
```

Required commands:

| Tool | Package | Purpose |
|------|---------|---------|
| `dd` | coreutils | Block-level disk copy |
| `sfdisk` | util-linux | Scriptable partition table editor |
| `e2fsck` | e2fsprogs | Filesystem check (required before resize) |
| `resize2fs` | e2fsprogs | Grow ext2/ext3/ext4 filesystem |
| `mkfs.ext3` | e2fsprogs | Create filesystem (alternative approach only) |
| `blkid` | util-linux | Show filesystem UUID |
| `lsblk` | util-linux | List block devices |
| `md5sum` | coreutils | Verify backup image |

### Important Note on WSL2

If using WSL2 on Windows, you must attach the USB SD card reader to the WSL
instance using `usbipd`:

```powershell
# PowerShell (Admin)
usbipd list                          # find the SD card reader bus ID
usbipd bind --busid <BUS_ID>
usbipd attach --wsl --busid <BUS_ID>
```

Then in WSL the card appears as `/dev/sdX`. Verify with `lsblk`.

---

## 4. Strategy — Recommended Approach

The recommended procedure clones the entire original image and resizes in-place:

1. **`dd`** the entire original 4 GB image onto the 32 GB card. The first
   ~3.7 GB are an exact clone; the rest is unallocated space.
2. **Expand p4** (extended partition) to fill the disk.
3. **Delete p5, p6, and p7** (logical partitions inside the extended).
4. **Recreate p5** at the same offset and size (~1.40 GB) — preserving device
   numbering and the backup partition.
5. **Recreate p6** using **all remaining space** (~27+ GB) — dropping p7 (swap)
   so p6 can fill the entire rest of the extended partition.
6. **`e2fsck` then `resize2fs`** on p6 to grow the ext3 filesystem to fill the
   new, larger partition.
7. **Proceed with normal commissioning** — the UUID is preserved.

### Why This Works

Since we clone with `dd` and only **resize** (never `mkfs` / reformat) the p6
filesystem, the ext3 filesystem UUID is preserved:

```
UUID="108a1490-8feb-4d0c-b3db-995dc5fc066c"
```

The commissioning script `commission-roomwizard.sh` locates rootfs by this UUID
(via `blkid -U`), so it continues to work without any modification.

### Why We Drop p7 (Swap)

The original layout includes a ~259 MB swap partition (p7). We drop it because:

- The RoomWizard has limited RAM (256 MB) and swap to SD card is extremely slow
- Gaming workloads benefit more from additional rootfs space than from swap
- Removing p7 lets p6 expand to fill the entire extended partition
- If swap is ever needed, a swap file can be created on the rootfs instead

### Why We Keep p5

The kernel boot parameter (`root=` in U-Boot or the kernel command line) may
reference the rootfs by **device path** (`/dev/mmcblk0p6`) rather than UUID. If
we deleted p5 and made rootfs the first logical partition, it would become
`mmcblk0p5` instead of `mmcblk0p6`, and the device would fail to boot.

By keeping p5 at its original size, rootfs remains `mmcblk0p6` and all device
path references are preserved. The ~1.40 GB used by p5 is a small price for
guaranteed compatibility.

> **Before starting:** Check the kernel command line on a working device:
> ```bash
> ssh root@<ip> "cat /proc/cmdline"
> ```
> Look for `root=/dev/mmcblk0p6` vs `root=UUID=108a1490-...`. If it uses UUID,
> device numbering doesn't matter. If it uses the device path, p6 numbering is
> **critical**.

### Alternative Approach (Safest)

If you want to avoid modifying any existing partition at all, see
[Section 6: Add Separate Game Partition (p8)](#6-alternative-add-separate-game-partition-p8).
This expands the extended container and adds a new logical partition for games,
keeping all 7 original partitions untouched (including swap).

---

## 5. Step-by-Step Procedure (Recommended: Expand rootfs)

> **⚠ WARNING:** Double-check every `/dev/sdX` reference. Writing to the wrong
> device will destroy data. Use `lsblk` to confirm device identity before every
> destructive operation.

### Step 1: Create Backup Image of Original SD Card

Insert the **original 4 GB SD card** into the Linux PC.

```bash
# Identify the device — look for the ~3.7 GB card
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT

# Set the device variable (CHANGE THIS to match your system)
ORIG=/dev/sdX

# Unmount all partitions if auto-mounted
sudo umount ${ORIG}* 2>/dev/null || true

# Create a full byte-for-byte backup image
sudo dd if=${ORIG} of=roomwizard-original-4gb.img bs=4M status=progress

# Record checksum for verification
md5sum roomwizard-original-4gb.img | tee roomwizard-original-4gb.img.md5
```

**Keep this image safe.** It is your recovery path if anything goes wrong.

### Step 2: Clone Image to 32 GB Card

Remove the 4 GB card. Insert the **new 32 GB SD card**.

```bash
# Identify the 32 GB card
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT

# Set the device variable (CHANGE THIS to match your system)
DEST=/dev/sdY

# Unmount all partitions if auto-mounted
sudo umount ${DEST}* 2>/dev/null || true

# Clone the 4 GB image onto the 32 GB card
sudo dd if=roomwizard-original-4gb.img of=${DEST} bs=4M status=progress

# Flush kernel caches
sync

# Force kernel to re-read the partition table
sudo partprobe ${DEST}
```

At this point the 32 GB card has an exact clone of the 4 GB layout in the first
~3.7 GB, with ~28 GB of unallocated space after it.

### Step 3: Save and Examine Current Partition Table

```bash
# Dump the current partition table in sfdisk-compatible format
sudo sfdisk -d ${DEST} > partition-table-before.txt
cat partition-table-before.txt
```

Expected output (from a real RoomWizard device — your device name will differ):

```
label: dos
label-id: 0x00000000
device: /dev/sdX
unit: sectors
sector-size: 512

/dev/sdX1 : start=          63, size=      144522, type=c, bootable
/dev/sdX2 : start=      144585, size=      514080, type=83
/dev/sdX3 : start=      658665, size=      498015, type=83
/dev/sdX4 : start=     1156680, size=     6586650, type=5
/dev/sdX5 : start=     1156743, size=     2939832, type=83
/dev/sdX6 : start=     4096638, size=     2008062, type=83
/dev/sdX7 : start=     6104763, size=      530082, type=82
```

> **Note:** The above is reference output from a real device. **Always verify
> your own `sfdisk -d` output** — sector values should match, but the device
> name will be whatever your system assigned (e.g., `/dev/sdb`, `/dev/sdc`).

Key values to record:

| Value | Sector | Purpose |
|-------|--------|---------|
| p1 start | 63 | Boot partition start (CHS-aligned) |
| p4 start | 1,156,680 | Extended container start |
| p5 start | 1,156,743 | Backup partition start |
| p5 size | 2,939,832 | Backup partition size (~1.40 GB) |
| p6 start | 4,096,638 | Rootfs start |
| p7 | 6,104,763 / 530,082 | Swap (will be dropped) |

### Step 4: Repartition (Expand Extended + rootfs, Drop Swap)

We will use `sfdisk` in script mode to:
1. Keep p1, p2, p3 exactly as they are
2. Expand p4 (extended) from the same start sector to the end of disk
3. Keep p5 at the same offset and size (preserving the backup partition)
4. Expand p6 to fill all remaining space in the extended partition
5. **Drop p7 (swap)** — not included in the new table, allowing p6 to grow larger

> **Why drop swap?** The original ~259 MB swap partition is not needed for our
> gaming use case. Removing it lets p6 expand to fill the entire rest of the
> extended partition. If swap is ever needed, create a swap file instead:
> `dd if=/dev/zero of=/swapfile bs=1M count=256 && mkswap /swapfile && swapon /swapfile`

```bash
# Get total sectors on the 32 GB card
TOTAL_SECTORS=$(sudo blockdev --getsz ${DEST})
echo "Total sectors: ${TOTAL_SECTORS}"
```

Now apply the new partition table. The key trick: **omit the size for p4 and p6**
so that `sfdisk` automatically fills them to the maximum available space.

```bash
# IMPORTANT: This will modify the partition table. Verify DEST is correct!
echo "Writing to ${DEST} — confirm this is the 32 GB SD card!"
lsblk ${DEST}

# Apply the new layout using sfdisk
# - p1-p3: exact copies from original
# - p4: same start, size omitted → fills to end of disk
# - p5: exact copy from original
# - p6: same start, size omitted → fills remaining space in extended
# - p7: DROPPED (was swap)
sudo sfdisk ${DEST} << 'EOF'
label: dos
label-id: 0x00000000
unit: sectors
sector-size: 512

/dev/sdX1 : start=          63, size=      144522, type=c, bootable
/dev/sdX2 : start=      144585, size=      514080, type=83
/dev/sdX3 : start=      658665, size=      498015, type=83
/dev/sdX4 : start=     1156680, type=5
/dev/sdX5 : start=     1156743, size=     2939832, type=83
/dev/sdX6 : start=     4096638, type=83
EOF

# Re-read partition table
sudo partprobe ${DEST}

# Verify
sudo sfdisk -l ${DEST}
```

> **Important:** Replace `/dev/sdX` in the heredoc with your actual device name
> (e.g., `/dev/sdb`). The `sfdisk` tool uses the device names in the input to
> match partition entries. Alternatively, the automated script
> [`clone-to-32gb.sh`](clone-to-32gb.sh) handles device naming automatically.

> **Verify:** The output should show p1–p3 unchanged, p4 expanded to fill the
> disk, p5 at the same location/size, p6 taking all remaining space (~27+ GB),
> and **no p7** (swap removed).

### Step 5: Verify and Resize Filesystem

The p6 partition is now much larger than its ext3 filesystem. We need to grow
the filesystem to fill the partition.

```bash
# REQUIRED: run e2fsck before resize2fs
sudo e2fsck -f ${DEST}6

# Grow the filesystem to fill the partition (no size argument = fill)
sudo resize2fs ${DEST}6
```

Expected output:

```
resize2fs 1.46.x (xx-xxx-xxxx)
Resizing the filesystem on /dev/sdY6 to XXXXXXX (4k) blocks.
The filesystem on /dev/sdY6 is now XXXXXXX (4k) blocks long.
```

### Step 6: Verify UUID Preserved

This is the critical check. The UUID **must** match the original:

```bash
sudo blkid ${DEST}6
```

Expected output:

```
/dev/sdY6: UUID="108a1490-8feb-4d0c-b3db-995dc5fc066c" TYPE="ext3"
```

If the UUID matches `108a1490-8feb-4d0c-b3db-995dc5fc066c`, the upgrade is
successful and commissioning will work unchanged.

> **If the UUID does NOT match**, see [Troubleshooting](#uuid-changed) below.

### Step 7: Verify All Partitions

Mount and quick-check each partition to ensure nothing was corrupted:

```bash
# Check all filesystems
sudo e2fsck -f ${DEST}2
sudo e2fsck -f ${DEST}3
sudo e2fsck -f ${DEST}5
sudo e2fsck -f ${DEST}6

# Verify sizes
sudo mkdir -p /mnt/rw-check
sudo mount ${DEST}6 /mnt/rw-check
df -h /mnt/rw-check
# Should show ~27 GB total
ls /mnt/rw-check/etc/  # Should show rootfs contents
sudo umount /mnt/rw-check
```

### Step 8: Run Normal Commissioning

Insert the 32 GB card into the RoomWizard and follow the standard workflow:

**Phase 1 — Commission** (SD card in Linux PC):

```bash
./commission-roomwizard.sh
```

This finds the rootfs by UUID `108a1490-8feb-4d0c-b3db-995dc5fc066c`, sets the
root password, enables SSH, and configures DHCP.

**Phase 2 — System Setup** (SSH to device):

```bash
./setup-device.sh <device-ip> --remove
```

Disables Steelcase services, installs init scripts, removes bloatware.

**Phase 3 — Deploy** (SSH to device):

```bash
./deploy-all.sh <device-ip>
```

Builds and deploys native\_apps, scummvm-roomwizard, usb\_host, vnc\_client.

---

## 6. Alternative: Add Separate Game Partition (p8)

This approach is the **safest** because it does not modify any existing
partition. All 7 original partitions remain byte-for-byte identical (including
the swap partition p7).

### Concept

After cloning the 4 GB image to the 32 GB card, the space beyond the original
extended partition (p4) is unallocated. Since all 4 primary partition slots are
already used (p1, p2, p3, p4), any new partition must be a logical partition
inside the extended (p4). So we expand p4 and add **p8** as a new logical
partition after p7.

### Procedure

**Steps 1–3** are identical to the recommended approach above. Then:

**Step 4: Expand only the extended partition, keep all 7 original partitions**

```bash
TOTAL_SECTORS=$(sudo blockdev --getsz ${DEST})

# Read existing table
sudo sfdisk -d ${DEST} > partition-table-before.txt

# Rewrite with p4 expanded to fill disk, all others unchanged (including p7 swap)
sudo sfdisk ${DEST} << 'EOF'
label: dos
label-id: 0x00000000
unit: sectors
sector-size: 512

/dev/sdX1 : start=          63, size=      144522, type=c, bootable
/dev/sdX2 : start=      144585, size=      514080, type=83
/dev/sdX3 : start=      658665, size=      498015, type=83
/dev/sdX4 : start=     1156680, type=5
/dev/sdX5 : start=     1156743, size=     2939832, type=83
/dev/sdX6 : start=     4096638, size=     2008062, type=83
/dev/sdX7 : start=     6104763, size=      530082, type=82
EOF

sudo partprobe ${DEST}
```

> **Note:** Replace `/dev/sdX` with your actual device name.

**Step 5: Create the new p8 partition**

```bash
# p8 starts after p7 ends, with alignment gap
P8_START=$(( 6104763 + 530082 + 63 ))

# Add p8 as a new logical partition
echo "${P8_START} - 83" | sudo sfdisk ${DEST} --append

sudo partprobe ${DEST}

# Format the new partition
sudo mkfs.ext3 -L games ${DEST}8
```

**Step 6: Add fstab entry on the device**

After commissioning (Phase 1) and before Phase 2, mount the rootfs and add an
fstab entry:

```bash
sudo mkdir -p /mnt/rw
sudo mount ${DEST}6 /mnt/rw

# Create mount point
sudo mkdir -p /mnt/rw/opt/roomwizard/games

# Get UUID of the new partition
GAMES_UUID=$(sudo blkid -s UUID -o value ${DEST}8)

# Add fstab entry
echo "UUID=${GAMES_UUID}  /opt/roomwizard/games  ext3  defaults,noatime  0  2" \
    | sudo tee -a /mnt/rw/etc/fstab

sudo umount /mnt/rw
```

### Trade-offs

| | Recommended (expand p6, drop swap) | Alternative (add p8, keep swap) |
|---|---|---|
| **Safety** | Modifies p4 and p6, drops p7 | Only expands p4 and adds new partition |
| **Complexity** | Simple — one big rootfs | Requires fstab entry and separate mount |
| **Game path** | `/opt/roomwizard/` (on rootfs) | `/opt/roomwizard/games/` (separate mount) |
| **Space** | ~27+ GB rootfs | ~981 MB rootfs + ~25 GB games partition |
| **Swap** | Removed (not needed for gaming) | Preserved (~259 MB) |
| **UUID preserved** | ✓ Yes (resize, not reformat) | ✓ Yes (p6 untouched) |
| **Deploy scripts** | No changes needed | ScummVM deploy must target `/opt/roomwizard/games/` |

---

## 7. New Partition Layout (After Upgrade)

### Recommended Layout (Expanded rootfs, swap dropped)

| Partition | Device | Type | Size | Mount | Purpose |
|-----------|--------|------|------|-------|---------|
| p1 | mmcblk0p1 | FAT32 | ~70.6 MB | /var/volatile/boot | Boot — MLO, U-Boot, uImage, DTB (unchanged, bootable) |
| p2 | mmcblk0p2 | ext3 | ~251 MB | /home/root/data | Application data (unchanged) |
| p3 | mmcblk0p3 | ext3 | ~243 MB | /home/root/log | System logs (unchanged) |
| p4 | — | extended | ~29 GB | — | Container for p5 + p6 (expanded) |
| p5 | mmcblk0p5 | ext3 | ~1.40 GB | /home/root/backup | OEM backup (unchanged, preserved for device numbering) |
| **p6** | **mmcblk0p6** | **ext3** | **~27+ GB** | **/** | **Root filesystem (expanded from ~981 MB!)** |
| ~~p7~~ | — | — | — | — | ~~Swap — removed to maximize rootfs space~~ |

- Total usable space on rootfs: **~27+ GB**
- After OS + apps: **~27 GB free** for games
- Device numbering: **preserved** — rootfs remains `mmcblk0p6`
- Rootfs UUID: **preserved** — `108a1490-8feb-4d0c-b3db-995dc5fc066c`
- Swap: **removed** — not needed for gaming workloads

### Alternative Layout (Separate game partition, swap preserved)

| Partition | Device | Type | Size | Mount | Purpose |
|-----------|--------|------|------|-------|---------|
| p1 | mmcblk0p1 | FAT32 | ~70.6 MB | /var/volatile/boot | Boot (unchanged, bootable) |
| p2 | mmcblk0p2 | ext3 | ~251 MB | /home/root/data | Data (unchanged) |
| p3 | mmcblk0p3 | ext3 | ~243 MB | /home/root/log | Logs (unchanged) |
| p4 | — | extended | ~29 GB | — | Container (expanded) |
| p5 | mmcblk0p5 | ext3 | ~1.40 GB | /home/root/backup | Backup (unchanged) |
| p6 | mmcblk0p6 | ext3 | ~981 MB | / | Rootfs (unchanged) |
| p7 | mmcblk0p7 | swap | ~259 MB | swap | Swap (unchanged) |
| **p8** | **mmcblk0p8** | **ext3** | **~25 GB** | **/opt/roomwizard/games** | **Game data (new)** |

---

## 8. Script Reference

The helper script [`clone-to-32gb.sh`](clone-to-32gb.sh) automates the
recommended procedure (Steps 2–6). It:

- Validates target device (block device, ≥16 GB, not `/dev/sda`, not mounted)
- Clones the source image/device with `dd` (or skips with `--expand-only`)
- Verifies the 7-partition layout and rootfs UUID
- Repartitions with `sfdisk` (expands p4 + p6, drops p7 swap, preserves p5)
- Preserves `label-id: 0x00000000` and the boot flag on p1
- Resizes the ext3 filesystem with `e2fsck` + `resize2fs`
- Verifies UUID is preserved after expansion

```bash
# Clone from image and expand:
sudo ./clone-to-32gb.sh --clone-from roomwizard-original-4gb.img /dev/sdb

# Clone from another SD card and expand:
sudo ./clone-to-32gb.sh --clone-from /dev/sdc /dev/sdb

# Expand only (image already cloned):
sudo ./clone-to-32gb.sh --expand-only /dev/sdb

# Dry run (show what would happen):
sudo ./clone-to-32gb.sh --dry-run --expand-only /dev/sdb

# Help:
./clone-to-32gb.sh --help
```

### Existing Scripts (Unchanged)

| Script | Phase | Purpose |
|--------|-------|---------|
| [`commission-roomwizard.sh`](commission-roomwizard.sh) | 1 | SD card commissioning — finds rootfs by UUID, sets password, SSH, DHCP |
| [`setup-device.sh`](setup-device.sh) | 2 | Disable Steelcase services, install init scripts, optional bloatware removal |
| [`deploy-all.sh`](deploy-all.sh) | 3 | Build and deploy all components to device |

None of these scripts require modification after the SD card upgrade (assuming
the recommended approach where UUID is preserved).

---

## 9. Troubleshooting

### UUID Changed

If you accidentally ran `mkfs.ext3` on p6 (reformatted instead of resized),
the UUID will be different and `commission-roomwizard.sh` won't find the rootfs.

**Option A — Restore the original UUID:**

```bash
# Set the UUID back to the original value
sudo tune2fs -U 108a1490-8feb-4d0c-b3db-995dc5fc066c /dev/sdY6
sudo blkid /dev/sdY6  # verify
```

**Option B — Update the commissioning script:**

Edit [`commission-roomwizard.sh`](commission-roomwizard.sh:50) and change the
UUID on line 50:

```bash
# Old:
ROOTFS=$(findmnt -rno TARGET --source "$(blkid -U 108a1490-8feb-4d0c-b3db-995dc5fc066c 2>/dev/null)" 2>/dev/null || echo "")

# New (replace with actual UUID from blkid):
ROOTFS=$(findmnt -rno TARGET --source "$(blkid -U <NEW-UUID-HERE> 2>/dev/null)" 2>/dev/null || echo "")
```

### Boot Fails After Repartitioning

**Symptom:** Device powers on but doesn't boot (no network, no SSH).

**Likely cause:** The kernel `root=` parameter references a device path that no
longer exists.

**Diagnosis — check kernel command line on a working device BEFORE upgrading:**

```bash
ssh root@<ip> "cat /proc/cmdline"
```

Look for:
- `root=/dev/mmcblk0p6` — **device path** (partition numbering matters!)
- `root=UUID=108a1490-...` — **UUID** (partition numbering doesn't matter)
- `root=PARTUUID=...` — **partition UUID** (different from filesystem UUID)

**Fix if using device path and you changed numbering:**

1. Insert the SD card back into the Linux PC
2. Mount the boot partition (p1, FAT32)
3. Check/edit the U-Boot environment or boot script:
   ```bash
   sudo mount /dev/sdY1 /mnt/boot
   ls /mnt/boot/
   # Look for: uEnv.txt, boot.scr, or boot.cmd
   cat /mnt/boot/uEnv.txt 2>/dev/null
   ```
4. If `root=/dev/mmcblk0p5` (wrong after renumbering), change to the correct
   device path

**Fix if boot partition is fine but rootfs is corrupted:**

```bash
sudo e2fsck -f /dev/sdY6
```

### Partition Alignment

The original RoomWizard layout uses **old CHS alignment** (partitions starting
at sector 63), not modern 2048-sector (1 MiB) alignment. This is typical for
older embedded Linux devices. Modern SD cards may perform slightly better with
4 KiB-aligned writes, but the sector 63 alignment has been working fine on
the RoomWizard hardware and we preserve it as-is.

To verify alignment:

```bash
sudo sfdisk -d /dev/sdY | grep start | while read line; do
    start=$(echo "$line" | grep -oP 'start=\s*\K[0-9]+')
    echo "Start: ${start}, 4K-aligned: $(( start % 8 == 0 ? 1 : 0 ))"
done
```

### sfdisk Fails or Reports Errors

If `sfdisk` refuses to write the new partition table:

```bash
# Force — wipe partition signatures first (DANGEROUS — backup first!)
sudo wipefs -a /dev/sdY4
sudo wipefs -a /dev/sdY5
sudo wipefs -a /dev/sdY6
sudo wipefs -a /dev/sdY7

# Then retry the sfdisk command from Step 4
```

Or use `fdisk` interactively as a fallback:

```bash
sudo fdisk /dev/sdY
# d → 7 (delete p7 / swap)
# d → 6 (delete p6)
# d → 5 (delete p5)
# d → 4 (delete p4)
# n → e → 4 (new extended, partition 4, start=1156680, full remaining size)
# n → l (new logical — becomes p5, start=1156743, size=+1435M)
# n → l (new logical — becomes p6, use remaining space)
# t → 5 → 83 (Linux)
# t → 6 → 83 (Linux)
# w (write)
```

### e2fsck or resize2fs Reports Errors

```bash
# If e2fsck finds errors, let it fix them:
sudo e2fsck -fy /dev/sdY6

# If resize2fs complains about filesystem state:
sudo e2fsck -f /dev/sdY6    # must pass clean check first
sudo resize2fs /dev/sdY6    # then resize
```

### Restoring From Backup Image

If anything goes wrong, restore the original image:

```bash
# Verify checksum
md5sum -c roomwizard-original-4gb.img.md5

# Restore to ANY SD card (4 GB or larger)
sudo dd if=roomwizard-original-4gb.img of=/dev/sdX bs=4M status=progress
sync
```

---

## 10. ScummVM Game Storage

### Game Location

Games are stored on the rootfs under:

```
/opt/roomwizard/scummvm-games/
```

Each game has its own subdirectory:

```
/opt/roomwizard/scummvm-games/
├── tentacle/          # Day of the Tentacle
├── dig/               # The Dig
├── ft/                # Full Throttle
├── grim/              # Grim Fandango
├── monkey1/           # Monkey Island 1
├── monkey2/           # Monkey Island 2
├── samnmax/           # Sam & Max Hit the Road
└── ...
```

### Available Space After Upgrade

| Scenario | rootfs Size | Used | Free for Games |
|----------|-----------|------|----------------|
| Stock 4 GB card | ~981 MB | ~463 MB | ~474 MB |
| 4 GB + bloatware removed | ~981 MB | ~285 MB | ~652 MB |
| **32 GB (recommended)** | **~27+ GB** | **~285 MB** | **~27 GB** |
| 32 GB (alternative p8) | ~981 MB + ~25 GB | ~285 MB | ~25 GB |

### Example Game Sizes (Classic SCUMM)

| Game | ScummVM ID | Size |
|------|-----------|------|
| Maniac Mansion | maniac | ~1 MB |
| Zak McKracken | zak | ~1 MB |
| Indiana Jones: Last Crusade | indy3 | ~3 MB |
| Loom | loom | ~5 MB |
| Monkey Island 1 (VGA) | monkey | ~12 MB |
| Monkey Island 2 | monkey2 | ~40 MB |
| Indiana Jones: Fate of Atlantis | atlantis | ~15 MB |
| Day of the Tentacle | tentacle | ~10 MB |
| Sam & Max Hit the Road | samnmax | ~30 MB |
| Full Throttle | ft | ~50 MB |
| The Dig | dig | ~500 MB |

### Example Game Sizes (Remastered / Later)

| Game | Size |
|------|------|
| Day of the Tentacle Remastered | ~2.5 GB |
| Full Throttle Remastered | ~5 GB |
| Grim Fandango Remastered | ~5.6 GB |
| Broken Sword 1 | ~700 MB |
| Beneath a Steel Sky | ~70 MB |

### Deploying Games via SCP

From your development machine:

```bash
# Single game
scp -r /path/to/game/data root@<device-ip>:/opt/roomwizard/scummvm-games/tentacle/

# Multiple games (recursive)
scp -r /path/to/all-games/* root@<device-ip>:/opt/roomwizard/scummvm-games/

# Verify on device
ssh root@<device-ip> "du -sh /opt/roomwizard/scummvm-games/*"
```

### Deploying Games via USB Drive

For very large transfers, a USB drive may be faster than SCP over Ethernet:

```bash
# On the device (after enabling USB host mode)
mount /dev/sda1 /mnt
cp -r /mnt/scummvm-games/* /opt/roomwizard/scummvm-games/
umount /mnt
```

---

## Quick Reference: Complete Command Sequence

For experienced users — the full recommended procedure in one block:

```bash
# === CONFIGURE THESE ===
ORIG=/dev/sdX          # Original 4 GB SD card device
DEST=/dev/sdY          # New 32 GB SD card device

# === STEP 1: Backup original ===
sudo umount ${ORIG}* 2>/dev/null || true
sudo dd if=${ORIG} of=roomwizard-original-4gb.img bs=4M status=progress
md5sum roomwizard-original-4gb.img > roomwizard-original-4gb.img.md5

# === STEP 2: Clone to 32 GB ===
sudo umount ${DEST}* 2>/dev/null || true
sudo dd if=roomwizard-original-4gb.img of=${DEST} bs=4M status=progress
sync && sudo partprobe ${DEST}

# === STEP 3: Record partition table ===
sudo sfdisk -d ${DEST} > partition-table-before.txt
cat partition-table-before.txt
# Should show 7 partitions (p1-p7) including swap on p7

# === STEP 4: Repartition (expand p4 + p6, drop p7 swap) ===
# NOTE: Replace /dev/sdX below with your actual device (e.g., /dev/sdb)
sudo sfdisk ${DEST} << 'PARTITION_EOF'
label: dos
label-id: 0x00000000
unit: sectors
sector-size: 512

/dev/sdX1 : start=          63, size=      144522, type=c, bootable
/dev/sdX2 : start=      144585, size=      514080, type=83
/dev/sdX3 : start=      658665, size=      498015, type=83
/dev/sdX4 : start=     1156680, type=5
/dev/sdX5 : start=     1156743, size=     2939832, type=83
/dev/sdX6 : start=     4096638, type=83
PARTITION_EOF
sudo partprobe ${DEST}

# === STEP 5: Resize filesystem ===
sudo e2fsck -f ${DEST}6
sudo resize2fs ${DEST}6

# === STEP 6: Verify ===
sudo blkid ${DEST}6
# Must show: UUID="108a1490-8feb-4d0c-b3db-995dc5fc066c"

echo "Done! Proceed with commission-roomwizard.sh → setup-device.sh → deploy-all.sh"
```
