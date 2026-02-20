# RoomWizard System Setup

**Goal:** take a stock RoomWizard, prepare the SD card on a Linux machine, boot the device, and deploy the games/ScummVM stack.

---

## 1. SD Card Preparation (Linux)

Pop the device open, remove the SD card, and insert it into a Linux machine.
Modern desktops auto-mount the partitions. Find them:

```bash
lsblk -o NAME,UUID,FSTYPE,SIZE,MOUNTPOINT | grep -v loop
```

The card has 6 partitions. You need **p6 — the rootfs** (ext4, ~1 GB):

| Partition | UUID | Mount | Purpose |
|---|---|---|---|
| p1 | `61E0-B73D` | *(boot)* | FAT32 — kernel images |
| p2 | `d5758df8-c160-4364-bac2-b68fcabeef30` | `/home/root/data` | App data |
| p3 | `da4cda60-1ee8-4497-9fb7-e36a1c963d7f` | `/home/root/log` | Logs |
| p5 | `26a7a226-6472-47fa-a205-a5fc2d22e149` | `/home/root/backup` | Firmware backup |
| **p6** | **`108a1490-8feb-4d0c-b3db-995dc5fc066c`** | **`/`** | **Root filesystem** |
| p7 | *(no UUID)* | swap | Swap |

```bash
# Locate p6 mountpoint (auto-mounted by desktop)
ROOTFS=$(findmnt -rno TARGET --source "$(blkid -U 108a1490-8feb-4d0c-b3db-995dc5fc066c)")
# or mount manually:
# sudo mkdir -p /mnt/rw && sudo mount /dev/sdX6 /mnt/rw && ROOTFS=/mnt/rw
echo $ROOTFS
```

---

## 2. Enable Root SSH Access

### Set the root password

```bash
openssl passwd -6 "yourpassword"   # generates a $6$... hash — copy the output

sudo nano $ROOTFS/etc/shadow
# Find root line (originally:  root:*:10933:0:99999:7:::)
# Replace the * with your hash: root:$6$xxxx...:10933:0:99999:7:::
```

### Allow root SSH login

```bash
sudo nano $ROOTFS/etc/ssh/sshd_config
# Ensure:
PermitRootLogin yes
PasswordAuthentication yes
PubkeyAuthentication yes
```

### Add your SSH public key (avoids password prompts)

```bash
sudo mkdir -p $ROOTFS/root/.ssh
sudo chmod 700 $ROOTFS/root/.ssh
cat ~/.ssh/id_rsa.pub | sudo tee $ROOTFS/root/.ssh/authorized_keys
sudo chmod 600 $ROOTFS/root/.ssh/authorized_keys
```

### Verify sshd starts at boot

```bash
ls $ROOTFS/etc/rc5.d/ | grep ssh    # expect S09sshd or similar
# If missing:
sudo ln -sf ../init.d/sshd $ROOTFS/etc/rc5.d/S09sshd
```

---

## 3. Enable DHCP

A stock device may have a static IP for its building-management network.
Switch to DHCP so it works on any network:

```bash
sudo nano $ROOTFS/etc/network/interfaces
# eth0 section should read:
#   auto eth0
#   iface eth0 inet dhcp
```

---

## 4. Unmount, Reinsert, Boot

```bash
sync
for mp in $(findmnt -rno TARGET | grep -E "108a1490|d5758df8|da4cda60|26a7a226"); do
    sudo umount "$mp"
done
```

Insert SD card, connect Ethernet, power on. Wait ~30 s.
Find the IP from your router DHCP leases, then:

```bash
ssh root@<device-ip>
```

---

## 5. Deploy Games and ScummVM

Run from your dev machine (WSL or Linux). See [native_games/README.md](native_games/README.md)
and [scummvm-roomwizard/README.md](scummvm-roomwizard/README.md) for build prerequisites.

### Native games (build + deploy in one step)

```bash
cd native_games
./build-and-deploy.sh <device-ip>            # build + deploy binaries + markers
./build-and-deploy.sh <device-ip> permanent  # + install boot service + reboot
```

The script cross-compiles all binaries, uploads them to `/opt/games/`, sets permissions,
and creates `.noargs`/`.hidden` marker files automatically.

### ScummVM (deploy separately — built from `scummvm/`)

```bash
DEVICE=root@<device-ip>
scp scummvm/scummvm                          $DEVICE:/opt/games/
scp scummvm-roomwizard/vkeybd_roomwizard.zip $DEVICE:/opt/games/
ssh $DEVICE 'chmod +x /opt/games/scummvm && touch /opt/games/scummvm.noargs && chmod 644 /opt/games/scummvm.noargs'
```

### Marker files

Game selector uses non-executable marker files to control how it launches or lists entries:

| Marker | Effect |
|---|---|
| `<name>.noargs` | Launch without device-path args (ScummVM opens devices itself) |
| `<name>.hidden` | Hide from menu entirely |

```bash
# ScummVM: no device args
ssh $DEVICE 'touch /opt/games/scummvm.noargs; chmod 644 /opt/games/scummvm.noargs'

# Hide dev tools
ssh $DEVICE 'for n in watchdog_feeder touch_test touch_debug touch_inject \
  touch_calibrate unified_calibrate pressure_test; do
    touch /opt/games/$n.hidden; chmod 644 /opt/games/$n.hidden; done'
```

---

## 6. Touch & Bezel Calibration

Run once per device:

```bash
ssh $DEVICE '/opt/games/unified_calibrate'
```

- **Phase 1:** tap 4 corner crosshairs → touch offset calibration
- **Phase 2:** tap `+`/`−` zones → bezel margin adjustment

Saves to `/etc/touch_calibration.conf`. Loaded automatically by games and ScummVM.

---

## 7. Permanent Game Mode

Replaces the browser/X11 stack with the game selector on every boot:

```bash
scp native_games/roomwizard-games-init.sh $DEVICE:/etc/init.d/roomwizard-games
ssh $DEVICE '
  chmod +x /etc/init.d/roomwizard-games
  update-rc.d browser remove
  update-rc.d x11 remove
  update-rc.d roomwizard-games defaults
  reboot'
```

**Revert to original browser mode:**

```bash
ssh $DEVICE '
  update-rc.d roomwizard-games remove
  update-rc.d browser defaults
  update-rc.d x11 defaults
  reboot'
```

---

## Watchdog

The hardware watchdog (`/dev/watchdog`, 60 s timeout) is fed automatically by
`/usr/sbin/watchdog`, which runs at boot via `/etc/init.d/watchdog`.
No custom feeder is needed — the system handles it regardless of which app is running.

**Expected running processes on a healthy game-mode device:**
```
init, udevd, syslogd, dbus-daemon, sshd, dhclient, cron,
/usr/sbin/watchdog, game_selector
```

---

## Partition Layout

```
mmcblk0p1  FAT32  64 MB   /var/volatile/boot  — kernel + boot files
mmcblk0p2  ext4   256 MB  /home/root/data     — application data
mmcblk0p3  ext4   250 MB  /home/root/log      — system logs
mmcblk0p5  ext3   1.5 GB  /home/root/backup   — firmware backup
mmcblk0p6  ext4   1.0 GB  /                   — root filesystem  ← edit this
mmcblk0p7  swap           —                   — swap
```

Full hardware details: [SYSTEM_ANALYSIS.md](SYSTEM_ANALYSIS.md)
