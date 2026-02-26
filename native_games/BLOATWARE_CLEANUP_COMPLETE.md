# RoomWizard Bloatware Cleanup Guide (Complete)

> **Disable unnecessary services and remove bloatware files to free RAM and disk space**

## Problem

The RoomWizard firmware includes:
1. **Watchdog system** that monitors browser/webserver stack (causes reboots in game mode)
2. **Bloatware files** (~178 MB) that are never used in game mode
3. **Vulnerable software** (Jetty 9.4.11, HSQLDB 2.0.0, outdated Java)

**Symptoms:**
- Device reboots unexpectedly every 3-4 hours
- 47% disk usage (405 MB used out of 931 MB)
- 45% RAM usage (106 MB used out of 234 MB)
- Security vulnerabilities in unused software

## Solution

Two-level cleanup:
1. **Level 1:** Disable services (frees ~80 MB RAM, prevents reboots) - **SAFE, REVERSIBLE**
2. **Level 2:** Remove files (frees ~178 MB disk, removes vulnerabilities) - **PERMANENT**

---

## Level 1: Service Cleanup (Safe, Reversible)

Disables services and watchdog checks without deleting files.

### Quick Fix

```bash
cd native_games
bash cleanup-bloatware.sh 192.168.50.73
```

**What it does:**
1. Disables watchdog test and repair binaries
2. Stops and disables: webserver, browser, x11, jetty, hsqldb, snmpd, vsftpd
3. Kills remaining Java/Xorg/browser processes

**Result:**
- ✅ No more unwanted reboots
- ✅ ~80 MB RAM freed (106 MB → 27 MB used)
- ✅ Only essential services running
- ✅ Fully reversible

---

## Level 2: File Removal (Aggressive, Permanent)

Removes bloatware files from filesystem. **Requires SD card backup to restore.**

### Enhanced Cleanup

```bash
cd native_games

# Step 1: Analyze (safe, no changes)
bash cleanup-bloatware-full.sh 192.168.50.73

# Step 2: Remove files (PERMANENT - requires confirmation)
bash cleanup-bloatware-full.sh 192.168.50.73 --remove-files
```

**What gets removed:**

| Package | Size | Reason | Security |
|---------|------|--------|----------|
| **Jetty 9.4.11** | 43 MB | Web server (not needed) | ⚠️ Vulnerable (CVE-2019-10241, CVE-2019-10247) |
| **OpenJRE 8** | 93 MB | Java runtime (not needed) | ⚠️ Outdated (2018 build) |
| **HSQLDB 2.0.0** | 3.5 MB | Database (not needed) | ⚠️ Vulnerable (CVE-2018-16336) |
| **CJK Fonts** | 31 MB | Chinese/Japanese/Korean fonts | Not needed for games |
| **X11 Data** | 5.2 MB | X11 resources | Games use framebuffer directly |
| **SNMP MIBs** | 2.5 MB | SNMP monitoring data | Not needed |
| **Total** | **~178 MB** | | |

**Result:**
- ✅ ~178 MB disk space freed (47% → 26% used, 474 MB → 652 MB free)
- ✅ Vulnerable software removed
- ✅ Cleaner system
- ⚠️ **PERMANENT** - requires SD card backup to restore

---

## Bloatware Analysis

### Disk Usage Before Cleanup
```
Filesystem      Size  Used Avail Use% Mounted on
/dev/root       931M  405M  474M  47% /
```

**Breakdown:**
- System files: ~227 MB (essential)
- Bloatware: ~178 MB (removable)
  - Jetty: 43 MB
  - OpenJRE: 93 MB
  - HSQLDB: 3.5 MB
  - CJK fonts: 31 MB
  - X11 data: 5.2 MB
  - SNMP: 2.5 MB

### Disk Usage After Full Cleanup
```
Filesystem      Size  Used Avail Use% Mounted on
/dev/root       931M  227M  652M  26% /
```

**Result:** 474 MB → 652 MB free (178 MB gained, 70% free space)

### Memory Usage Before Cleanup
```
Mem:  234Mi  106Mi   60Mi  (45% used)
Swap: 258Mi    3Mi  255Mi
```

### Memory Usage After Cleanup
```
Mem:  234Mi   27Mi  188Mi  (12% used)
Swap: 258Mi    3Mi  255Mi
```

**Result:** 106 MB → 27 MB used (80 MB freed, 80% free memory)

---

## Security Vulnerabilities Removed

### Jetty 9.4.11 (2018)
- **CVE-2019-10241** - Directory traversal vulnerability
- **CVE-2019-10247** - Timing channel vulnerability
- **CVE-2019-10246** - Information disclosure
- **Status:** Outdated, no longer maintained on this version

### HSQLDB 2.0.0 (2010)
- **CVE-2018-16336** - Remote code execution via SQL injection
- **Status:** Ancient version, multiple known vulnerabilities

### OpenJRE 8 (2018 build)
- **Multiple CVEs** - Outdated Java 8 build from 2018
- **Status:** No security updates, vulnerable to known exploits

**Recommendation:** Remove these packages if not using browser mode.

---

## What Gets Disabled

### Watchdog Changes
```bash
# /etc/watchdog.conf (before)
test-binary = /opt/sbin/watchdog_test
repair-binary = /opt/sbin/watchdog_repair

# /etc/watchdog.conf (after)
#test-binary = /opt/sbin/watchdog_test
#repair-binary = /opt/sbin/watchdog_repair
```

**Note:** Hardware watchdog daemon (`/usr/sbin/watchdog`) continues running and feeding `/dev/watchdog`. Only test/repair scripts are disabled.

### Services Disabled

| Service | Purpose | Why Disable |
|---------|---------|-------------|
| **webserver** | Jetty web server | Not needed in game mode |
| **browser** | WebKit browser | Not needed in game mode |
| **x11** | X11 display server | Games use framebuffer directly |
| **jetty** | Java servlet container | Not needed in game mode |
| **hsqldb** | Database server | Not needed in game mode |
| **snmpd** | SNMP monitoring | Not needed for games |
| **vsftpd** | FTP server | Not needed for games |

### Services Kept Running

| Service | Purpose | Why Keep |
|---------|---------|----------|
| **watchdog** | Hardware watchdog feeder | Critical - prevents hardware reset |
| **sshd** | SSH server | Remote access |
| **cron** | Task scheduler | System maintenance |
| **dbus** | Message bus | System IPC |
| **ntpd** | Time synchronization | Keep time accurate |
| **audio-enable** | Audio amplifier init | Required for sound |
| **roomwizard-games** | Game selector | Main application |

---

## Verification

### Check Service Cleanup

```bash
ssh root@192.168.50.73

# Verify watchdog config
grep -E '^(test-binary|repair-binary)' /etc/watchdog.conf
# Should show: (no output - lines are commented)

# Check running services
ls /etc/rc5.d/S* | grep -v -E 'watchdog|roomwizard|audio|ssh|cron|dbus|ntpd'
# Should show minimal services

# Check running processes
ps aux | grep -E 'java|Xorg|browser|jetty'
# Should show: (no matches)

# Check memory usage
free -h
# Should show: ~27 MB used, ~188 MB available
```

### Check File Removal

```bash
ssh root@192.168.50.73

# Check bloatware status
ls -d /opt/jetty-9-4-11 2>/dev/null && echo "Jetty: present" || echo "Jetty: removed"
ls -d /opt/openjre-8 2>/dev/null && echo "OpenJRE: present" || echo "OpenJRE: removed"
ls -d /opt/hsqldb 2>/dev/null && echo "HSQLDB: present" || echo "HSQLDB: removed"

# Check disk usage
df -h /
# Should show: ~26% used, ~652 MB free (if files removed)

# View removal log
cat /root/bloatware-removed.txt
```

---

## Reverting Changes

### Revert Service Changes (Level 1)

```bash
ssh root@192.168.50.73

# Restore watchdog config
cp /etc/watchdog.conf.backup /etc/watchdog.conf
/etc/init.d/watchdog restart

# Re-enable browser mode
update-rc.d browser defaults
update-rc.d x11 defaults
update-rc.d webserver defaults
update-rc.d roomwizard-games remove

# Reboot
reboot
```

### Restore Files (Level 2)

**Cannot be done without SD card backup.** File removal is permanent.

To restore:
1. Power off device
2. Remove SD card
3. Flash original SD card image
4. Reinsert and boot

---

## Permanent Fix (New Deployments)

The cleanup is integrated into the deployment script:

```bash
cd native_games
./build-and-deploy.sh 192.168.50.73 permanent
```

The `permanent` mode automatically:
1. Disables watchdog test/repair
2. Stops and disables unnecessary services
3. Installs game mode boot service
4. Reboots into lean game mode

**Note:** File removal is NOT automatic - use `cleanup-bloatware-full.sh --remove-files` separately if desired.

---

## Technical Details

### Watchdog Error Codes

| Code | Meaning | Cause |
|------|---------|-------|
| 100 | Database failure | HSQLDB not responding |
| 112 | Webserver failure | Browser/Jetty not running |

### Watchdog Behavior

1. **Test Binary** (`/opt/sbin/watchdog_test`)
   - Runs every 5 minutes
   - Checks database connectivity (exit 100 if fail)
   - Checks webserver/browser (exit 112 if fail)

2. **Repair Binary** (`/opt/sbin/watchdog_repair`)
   - Triggered on test failure
   - Attempts to restart failed service
   - If repair fails, triggers reboot

3. **Grace Period**
   - After first failure, enters "grace period"
   - Allows multiple failures before rebooting
   - Typically reboots after 3-4 hours of continuous failures

### Why This Happens

The RoomWizard firmware was designed for **browser mode** (room scheduling display). The watchdog expects:
- X11 display server running
- WebKit browser running
- Jetty web server running
- HSQLDB database running

In **game mode**, these services are intentionally disabled. The watchdog sees this as a "failure" and triggers reboots.

**Solution:** Disable watchdog test/repair while keeping hardware watchdog feeder active.

---

## Best Practices

### For Development
- Run Level 1 cleanup after initial deployment
- Keeps system lean and stable
- Prevents interruptions during testing
- Consider Level 2 if disk space is limited

### For Production
- Use `./build-and-deploy.sh <ip> permanent` for new deployments
- Level 1 cleanup is automatic
- Level 2 (file removal) is optional but recommended
- Always keep SD card backup before Level 2

### For Security
- Remove vulnerable software (Jetty, HSQLDB, old Java)
- Reduces attack surface
- No network services listening
- Only SSH exposed

---

## Troubleshooting

### Device Still Reboots

Check if watchdog test is still active:
```bash
ssh root@192.168.50.73 'grep "^test-binary" /etc/watchdog.conf'
```

If uncommented, re-run cleanup:
```bash
bash native_games/cleanup-bloatware.sh 192.168.50.73
```

### Services Restart After Reboot

Ensure services are removed from rc5.d:
```bash
ssh root@192.168.50.73 'ls /etc/rc5.d/ | grep -E "browser|x11|webserver"'
```

If present:
```bash
ssh root@192.168.50.73 'update-rc.d browser remove; update-rc.d x11 remove; update-rc.d webserver remove'
```

### Memory Usage Still High

Check for remaining Java processes:
```bash
ssh root@192.168.50.73 'ps aux | grep java'
```

Kill if found:
```bash
ssh root@192.168.50.73 'killall java'
```

### Cannot Remove Files

If file removal fails, check:
```bash
ssh root@192.168.50.73 'lsof | grep -E "jetty|hsqldb|openjre"'
```

Stop all services first:
```bash
ssh root@192.168.50.73 'killall java; /etc/init.d/webserver stop; /etc/init.d/jetty stop'
```

---

## Summary

**Problem:** Watchdog triggers reboots, bloatware wastes space, vulnerable software  
**Solution:** Disable watchdog test/repair, stop services, optionally remove files  
**Result:** Stable game mode, 80 MB RAM freed, 178 MB disk freed, vulnerabilities removed  

**Tools:**
- `cleanup-bloatware.sh` - Level 1 (services only)
- `cleanup-bloatware-full.sh` - Level 1 + 2 (services + files)
- `build-and-deploy.sh permanent` - Automatic Level 1 for new deployments

**Recommendation:** Always do Level 1. Do Level 2 if you want maximum space/security and have SD backup.

---

*For more information, see:*
- [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md) - Complete hardware analysis
- [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md) - Initial device setup
- [`README.md`](README.md) - Native games documentation
