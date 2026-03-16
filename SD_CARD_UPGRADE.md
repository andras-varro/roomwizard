# SD Card Upgrade: 4 GB → 32 GB

Upgrading the RoomWizard SD card from the stock 4 GB to a 32 GB SDHC card to
accommodate large ScummVM game data (Full Throttle, The Dig, Grim Fandango, etc.).

---

## Table of Contents

1. [Overview](#1-overview)
2. [Prerequisites](#2-prerequisites)
3. [Strategy — Recommended Approach](#3-strategy--recommended-approach)
4. [Step-by-Step Procedure (Recommended: Expand rootfs)](#4-step-by-step-procedure-recommended-expand-rootfs)
5. [Alternative: Add Separate Game Partition (p7)](#5-alternative-add-separate-game-partition-p7)
6. [New Partition Layout (After Upgrade)](#6-new-partition-layout-after-upgrade)
7. [Script Reference](#7-script-reference)
8. [Troubleshooting](#8-troubleshooting)
9. [ScummVM Game Storage](#9-scummvm-game-storage)

---

## 1. Overview

### Why Upgrade

The stock RoomWizard SD card is a ~3.7 GB (nominal 4 GB) card. The root
filesystem (p6) is only **1024 MB**, of which ~463 MB is used out of the box
(~474 MB free). Even after removing ~178 MB of Steelcase bloatware with
`setup-device.sh --remove`, only ~652 MB is free.

ScummVM games are deployed to `/opt/roomwizard/` on the rootfs. Large adventure
games require significant space:

| Game | Approximate Size |
|------|-----------------|
| Day of the Tentacle Remastered | ~2.5 GB |
| Full Throttle Remastered | ~5 GB |
| Grim Fandango Remastered | ~5.6 GB |
| The Dig | ~500 MB |
| Sam & Max Hit the Road | ~300 MB |
| Monkey Island SE | ~1.5 GB |
| Classic LucasArts (SCUMM) | 5–50 MB each |

With a 32 GB card and an expanded rootfs, there is ~27 GB of usable space —
enough for a large library of both classic and remastered titles.

### What Changes

- The **extended partition (p4)** is expanded to fill the entire 32 GB card.
- The **rootfs partition (p6)** is expanded from 1024 MB to ~27.5 GB.
- The ext3 filesystem on p6 is resized in-place (no reformat).

### What Stays the Same

- **Boot partition (p1)** — FAT32, 64 MB, MLO / U-Boot / uImage / DTB — untouched.
- **Data partition (p2)** — ext3, 256 MB — untouched.
- **Log partition (p3)** — ext3, 250 MB — untouched.
- **Backup partition (p5)** — ext3, 1500 MB — kept at original size for safety.
- **Kernel, U-Boot, device tree** — unchanged.
- **Commissioning workflow** — `commission-roomwizard.sh` → `setup-device.sh` → `deploy-all.sh` works identically because the rootfs UUID is preserved.

---

## 2. Prerequisites

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

## 3. Strategy — Recommended Approach

The recommended procedure clones the entire original image and resizes in-place:

1. **`dd`** the entire original 4 GB image onto the 32 GB card. The first
   ~3.7 GB are an exact clone; the rest is unallocated space.
2. **Expand p4** (extended partition) to fill the disk.
3. **Delete p5 and p6** (logical partitions inside the extended).
4. **Recreate p5** at the same offset and size (1500 MB) — preserving device
   numbering and the backup partition.
5. **Recreate p6** using **all remaining space** (~27.5 GB).
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

### Why We Keep p5

The kernel boot parameter (`root=` in U-Boot or the kernel command line) may
reference the rootfs by **device path** (`/dev/mmcblk0p6`) rather than UUID. If
we deleted p5 and made rootfs the first logical partition, it would become
`mmcblk0p5` instead of `mmcblk0p6`, and the device would fail to boot.

By keeping p5 at its original size, rootfs remains `mmcblk0p6` and all device
path references are preserved. The 1500 MB used by p5 is a small price for
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
[Section 5: Add Separate Game Partition (p7)](#5-alternative-add-separate-game-partition-p7).
This creates a new logical partition in the unallocated space and mounts it
separately. It requires adding an fstab entry but doesn't touch existing
partitions.

---

## 4. Step-by-Step Procedure (Recommended: Expand rootfs)

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

Expected output (approximate sector values):

```
label: dos
label-id: 0x...
device: /dev/sdY
unit: sectors

/dev/sdY1 : start=       2048, size=     131072, type=c
/dev/sdY2 : start=     133120, size=     524288, type=83
/dev/sdY3 : start=     657408, size=     512000, type=83
/dev/sdY4 : start=    1169408, size=    5949440, type=5
/dev/sdY5 : start=    1171456, size=    3072000, type=83
/dev/sdY6 : start=    4245504, size=    2097152, type=83
```

Record these values — especially the **start sector of p4** (`1169408` in this
example), the **start sector of p5** (`1171456`), and the **size of p5**
(`3072000`).

> **Note:** Your actual sector values may differ. Use the values from YOUR
> `sfdisk -d` output in all subsequent commands.

### Step 4: Repartition (Expand Extended + rootfs)

We will use `sfdisk` in script mode to:
1. Delete logical partitions p5 and p6
2. Delete extended partition p4
3. Recreate p4 from the same start sector to the end of disk
4. Recreate p5 at the same offset and size (preserving the backup partition)
5. Recreate p6 using all remaining space

```bash
# Get total sectors on the 32 GB card
TOTAL_SECTORS=$(sudo blockdev --getsz ${DEST})
echo "Total sectors: ${TOTAL_SECTORS}"

# Record values from your partition-table-before.txt:
P4_START=1169408       # start sector of p4 (extended)
P5_START=1171456       # start sector of p5 (backup)
P5_SIZE=3072000        # size of p5 in sectors (1500 MB)

# Calculate p6 start: after p5 ends, plus 2048-sector gap for alignment
P6_START=$(( P5_START + P5_SIZE + 2048 ))
echo "p6 will start at sector: ${P6_START}"

# Extended partition size: from p4 start to end of disk
P4_SIZE=$(( TOTAL_SECTORS - P4_START ))

# p6 size: from p6 start to end of extended partition
# (extended ends at P4_START + P4_SIZE, minus the 2-sector MBR overhead)
P6_SIZE=$(( P4_START + P4_SIZE - P6_START ))

echo "New layout:"
echo "  p4 (extended): start=${P4_START}, size=${P4_SIZE}"
echo "  p5 (backup):   start=${P5_START}, size=${P5_SIZE}"
echo "  p6 (rootfs):   start=${P6_START}, size=${P6_SIZE}"
```

Now apply the new partition table:

```bash
# IMPORTANT: This will modify the partition table. Verify DEST is correct!
echo "Writing to ${DEST} — confirm this is the 32 GB SD card!"
lsblk ${DEST}

# Apply the new layout using sfdisk
sudo sfdisk ${DEST} << EOF
label: dos

/dev/${DEST##*/}1 : start=       2048, size=     131072, type=c
/dev/${DEST##*/}2 : start=     133120, size=     524288, type=83
/dev/${DEST##*/}3 : start=     657408, size=     512000, type=83
/dev/${DEST##*/}4 : start=  ${P4_START}, size=  ${P4_SIZE}, type=5
/dev/${DEST##*/}5 : start=  ${P5_START}, size=  ${P5_SIZE}, type=83
/dev/${DEST##*/}6 : start=  ${P6_START}, size=  ${P6_SIZE}, type=83
EOF

# Re-read partition table
sudo partprobe ${DEST}

# Verify
sudo sfdisk -l ${DEST}
```

> **Verify:** The output should show p1–p3 unchanged, p4 expanded to fill the
> disk, p5 at the same location/size, and p6 taking all remaining space
> (~27.5 GB).

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

## 5. Alternative: Add Separate Game Partition (p7)

This approach is the **safest** because it does not modify any existing
partition. All original partitions remain byte-for-byte identical.

### Concept

After cloning the 4 GB image to the 32 GB card, the space beyond the original
extended partition (p4) is unallocated. We create a **new primary partition**
(p7 is not possible in MBR with 4 primary slots used — so instead we add a new
**logical partition** inside the expanded extended).

Actually, since all 4 primary partition slots are already used (p1, p2, p3, p4),
any new partition must be a logical partition inside the extended (p4). So we
expand p4 and add p7 as a new logical partition after p6.

### Procedure

**Steps 1–3** are identical to the recommended approach above. Then:

**Step 4: Expand only the extended partition**

```bash
TOTAL_SECTORS=$(sudo blockdev --getsz ${DEST})
P4_START=1169408  # from your sfdisk dump

# Read existing table
sudo sfdisk -d ${DEST} > partition-table-before.txt

# Modify only p4's size to fill the disk, keep everything else
P4_SIZE=$(( TOTAL_SECTORS - P4_START ))

sudo sfdisk ${DEST} << EOF
label: dos

/dev/${DEST##*/}1 : start=       2048, size=     131072, type=c
/dev/${DEST##*/}2 : start=     133120, size=     524288, type=83
/dev/${DEST##*/}3 : start=     657408, size=     512000, type=83
/dev/${DEST##*/}4 : start=  ${P4_START}, size=  ${P4_SIZE}, type=5
/dev/${DEST##*/}5 : start=  1171456, size=  3072000, type=83
/dev/${DEST##*/}6 : start=  4245504, size=  2097152, type=83
EOF

sudo partprobe ${DEST}
```

**Step 5: Create the new p7 partition**

```bash
# p7 starts after p6 ends, with alignment gap
P7_START=$(( 4245504 + 2097152 + 2048 ))
P7_SIZE=$(( P4_START + P4_SIZE - P7_START ))

# Add p7 as a new logical partition
echo "${P7_START} ${P7_SIZE} 83" | sudo sfdisk ${DEST} --append

sudo partprobe ${DEST}

# Format the new partition
sudo mkfs.ext3 -L games ${DEST}7
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
GAMES_UUID=$(sudo blkid -s UUID -o value ${DEST}7)

# Add fstab entry
echo "UUID=${GAMES_UUID}  /opt/roomwizard/games  ext3  defaults,noatime  0  2" \
    | sudo tee -a /mnt/rw/etc/fstab

sudo umount /mnt/rw
```

### Trade-offs

| | Recommended (expand p6) | Alternative (add p7) |
|---|---|---|
| **Safety** | Modifies p4 and p6 | Only adds new partition |
| **Complexity** | Simple — one big rootfs | Requires fstab entry and separate mount |
| **Game path** | `/opt/roomwizard/` (on rootfs) | `/opt/roomwizard/games/` (separate mount) |
| **Space** | ~27.5 GB rootfs | ~1 GB rootfs + ~26 GB games partition |
| **UUID preserved** | ✓ Yes (resize, not reformat) | ✓ Yes (p6 untouched) |
| **Deploy scripts** | No changes needed | ScummVM deploy must target `/opt/roomwizard/games/` |

---

## 6. New Partition Layout (After Upgrade)

### Recommended Layout (Expanded rootfs)

| Partition | Device | Type | Size | Mount | Purpose |
|-----------|--------|------|------|-------|---------|
| p1 | mmcblk0p1 | FAT32 | 64 MB | /var/volatile/boot | Boot — MLO, U-Boot, uImage, DTB (unchanged) |
| p2 | mmcblk0p2 | ext3 | 256 MB | /home/root/data | Application data (unchanged) |
| p3 | mmcblk0p3 | ext3 | 250 MB | /home/root/log | System logs (unchanged) |
| p4 | — | extended | ~29 GB | — | Container for p5 + p6 (expanded) |
| p5 | mmcblk0p5 | ext3 | 1500 MB | /home/root/backup | OEM backup (unchanged, preserved for device numbering) |
| **p6** | **mmcblk0p6** | **ext3** | **~27.5 GB** | **/** | **Root filesystem (expanded from 1024 MB!)** |

- Total usable space on rootfs: **~27.5 GB**
- After OS + apps: **~27 GB free** for games
- Device numbering: **preserved** — rootfs remains `mmcblk0p6`
- Rootfs UUID: **preserved** — `108a1490-8feb-4d0c-b3db-995dc5fc066c`

### Alternative Layout (Separate game partition)

| Partition | Device | Type | Size | Mount | Purpose |
|-----------|--------|------|------|-------|---------|
| p1 | mmcblk0p1 | FAT32 | 64 MB | /var/volatile/boot | Boot (unchanged) |
| p2 | mmcblk0p2 | ext3 | 256 MB | /home/root/data | Data (unchanged) |
| p3 | mmcblk0p3 | ext3 | 250 MB | /home/root/log | Logs (unchanged) |
| p4 | — | extended | ~29 GB | — | Container (expanded) |
| p5 | mmcblk0p5 | ext3 | 1500 MB | /home/root/backup | Backup (unchanged) |
| p6 | mmcblk0p6 | ext3 | 1024 MB | / | Rootfs (unchanged) |
| **p7** | **mmcblk0p7** | **ext3** | **~26 GB** | **/opt/roomwizard/games** | **Game data (new)** |

---

## 7. Script Reference

The helper script [`clone-to-32gb.sh`](clone-to-32gb.sh) automates the
recommended procedure (Steps 2–6). It:

- Validates target device (block device, ≥16 GB, not `/dev/sda`, not mounted)
- Clones the source image/device with `dd` (or skips with `--expand-only`)
- Verifies the 6-partition layout and rootfs UUID
- Repartitions with `sfdisk` (expands p4 + p6, preserves p5)
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

## 8. Troubleshooting

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

Modern SD cards perform best with 4 KiB-aligned partitions (start sectors
divisible by 8). The original RoomWizard layout uses 2048-sector alignment
(1 MiB), which is optimal. The steps above preserve this alignment.

To verify:

```bash
sudo sfdisk -d /dev/sdY | grep start | while read line; do
    start=$(echo "$line" | grep -oP 'start=\s*\K[0-9]+')
    echo "Start: ${start}, aligned: $(( start % 2048 == 0 ? 1 : 0 ))"
done
```

### sfdisk Fails or Reports Errors

If `sfdisk` refuses to write the new partition table:

```bash
# Force — wipe partition signatures first (DANGEROUS — backup first!)
sudo wipefs -a /dev/sdY4
sudo wipefs -a /dev/sdY5
sudo wipefs -a /dev/sdY6

# Then retry the sfdisk command from Step 4
```

Or use `fdisk` interactively as a fallback:

```bash
sudo fdisk /dev/sdY
# d → 6 (delete p6)
# d → 5 (delete p5)
# d → 4 (delete p4)
# n → e → 4 (new extended, partition 4, same start, full size)
# n → l (new logical — becomes p5, same start/size as old p5)
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

## 9. ScummVM Game Storage

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
| Stock 4 GB card | 1024 MB | ~463 MB | ~474 MB |
| 4 GB + bloatware removed | 1024 MB | ~285 MB | ~652 MB |
| **32 GB (recommended)** | **~27.5 GB** | **~285 MB** | **~27 GB** |
| 32 GB (alternative p7) | 1024 MB + ~26 GB | ~285 MB | ~26 GB |

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
# NOTE: Record P4_START, P5_START, P5_SIZE, P6_START from output

# === STEP 4: Repartition ===
TOTAL_SECTORS=$(sudo blockdev --getsz ${DEST})
P4_START=1169408       # ← from your sfdisk dump
P5_START=1171456       # ← from your sfdisk dump
P5_SIZE=3072000        # ← from your sfdisk dump
P6_START=$(( P5_START + P5_SIZE + 2048 ))
P4_SIZE=$(( TOTAL_SECTORS - P4_START ))
P6_SIZE=$(( P4_START + P4_SIZE - P6_START ))

sudo sfdisk ${DEST} << EOF
label: dos
${DEST}1 : start=       2048, size=     131072, type=c
${DEST}2 : start=     133120, size=     524288, type=83
${DEST}3 : start=     657408, size=     512000, type=83
${DEST}4 : start=  ${P4_START}, size=  ${P4_SIZE}, type=5
${DEST}5 : start=  ${P5_START}, size=  ${P5_SIZE}, type=83
${DEST}6 : start=  ${P6_START}, size=  ${P6_SIZE}, type=83
EOF
sudo partprobe ${DEST}

# === STEP 5: Resize filesystem ===
sudo e2fsck -f ${DEST}6
sudo resize2fs ${DEST}6

# === STEP 6: Verify ===
sudo blkid ${DEST}6
# Must show: UUID="108a1490-8feb-4d0c-b3db-995dc5fc066c"

echo "Done! Proceed with commission-roomwizard.sh → setup-device.sh → deploy-all.sh"
```
