# RoomWizard Bloatware Cleanup Guide

> **Disable unnecessary services and prevent unwanted reboots in game mode**

## Problem

The RoomWizard firmware includes a watchdog system that monitors the browser/webserver stack. When running in game mode (without browser/X11), the watchdog detects "failures" and triggers automatic reboots every few hours.

**Symptoms:**
- Device reboots unexpectedly every 3-4 hours
- Log messages: `WATCHDOG: Test binary executed with exit code 112`
- Log messages: `WATCHDOG_REPAIR: Watchdog error 112 detected during watchdog repair execution`
- Log messages: `WATCHDOG: watchdog_repair script returned with error. Rebooting device..`

## Solution

Disable the watchdog test/repair binaries and stop unnecessary services.

### Quick Fix (Immediate)

Run the cleanup script on an already-deployed device:

```bash
cd native_games
bash cleanup-bloatware.sh 192.168.50.73
```

**What it does:**
1. Disables watchdog test and repair binaries in `/etc/watchdog.conf`
2. Stops and disables: webserver, browser, x11, jetty, hsqldb, snmpd, vsftpd
3. Kills remaining Java/Xorg/browser processes
4. Shows current status and memory usage

**Result:**
- No more unwanted reboots
- ~80 MB RAM freed (from 106 MB used to 27 MB used)
- Only essential services running

### Permanent Fix (New Deployments)

The cleanup is now integrated into the deployment script:

```bash
cd native_games
./build-and-deploy.sh 192.168.50.73 permanent
```

The `permanent` mode now automatically:
1. Disables watchdog test/repair
2. Stops and disables unnecessary services
3. Installs game mode boot service
4. Reboots into lean game mode

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

**Note:** The hardware watchdog daemon (`/usr/sbin/watchdog`) continues running and feeding `/dev/watchdog`. Only the test/repair scripts are disabled.

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

## Memory Impact

**Before Cleanup:**
```
Mem:  234Mi  106Mi   60Mi  (45% used)
Swap: 258Mi    3Mi  255Mi
```

**After Cleanup:**
```
Mem:  234Mi   27Mi  188Mi  (12% used)
Swap: 258Mi    3Mi  255Mi
```

**Savings:** ~80 MB RAM freed (from 106 MB to 27 MB used)

## Verification

Check that cleanup was successful:

```bash
ssh root@192.168.50.73

# Verify watchdog config
grep -E '^(test-binary|repair-binary)' /etc/watchdog.conf
# Should show: (no output - lines are commented)

# Check running services
ls /etc/rc5.d/S* | grep -v -E 'watchdog|roomwizard|audio|ssh|cron|dbus'
# Should show minimal services

# Check running processes
ps aux | grep -E 'java|Xorg|browser|jetty'
# Should show: (no matches)

# Check memory usage
free -h
# Should show: ~27 MB used, ~188 MB available
```

## Reverting Changes

If you need to restore the original configuration:

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

The RoomWizard firmware was designed for **browser mode** (room scheduling display). The watchdog system expects:
- X11 display server running
- WebKit browser running
- Jetty web server running
- HSQLDB database running

In **game mode**, these services are intentionally disabled (games use framebuffer directly). The watchdog sees this as a "failure" and triggers reboots.

**Solution:** Disable the watchdog test/repair while keeping the hardware watchdog feeder active.

## Best Practices

### For Development
- Run cleanup script after initial deployment
- Keeps system lean and stable
- Prevents interruptions during testing

### For Production
- Use `./build-and-deploy.sh <ip> permanent` for new deployments
- Cleanup is automatic
- System boots directly into game mode

### For Debugging
- Keep SSH access enabled (sshd)
- Monitor logs: `tail -f /var/log/messages`
- Check watchdog status: `ps aux | grep watchdog`

## Troubleshooting

### Device Still Reboots

Check if watchdog test is still active:
```bash
ssh root@192.168.50.73 'grep "^test-binary" /etc/watchdog.conf'
```

If it shows uncommented line, re-run cleanup:
```bash
bash native_games/cleanup-bloatware.sh 192.168.50.73
```

### Services Restart After Reboot

Ensure services are removed from rc5.d:
```bash
ssh root@192.168.50.73 'ls /etc/rc5.d/ | grep -E "browser|x11|webserver"'
```

If services are still there:
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

## Summary

**Problem:** Watchdog triggers reboots when browser/webserver not running  
**Solution:** Disable watchdog test/repair, stop unnecessary services  
**Result:** Stable game mode, 80 MB RAM freed, no unwanted reboots  
**Tools:** `cleanup-bloatware.sh` (immediate) or `build-and-deploy.sh permanent` (new deployments)

---

*For more information, see:*
- [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md) - Complete hardware analysis
- [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md) - Initial device setup
- [`README.md`](README.md) - Native games documentation
