# LED Control Quick Start Guide

## What Was Implemented

A Java servlet backend that allows the brick breaker game's JavaScript to control the RoomWizard's red and green LEDs.

## Files Created/Modified

### New Files
1. **`LEDControlServlet.java`** - Java servlet for LED control
   - Location: `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes/`
   - Purpose: Provides HTTP endpoint to control LEDs

### Modified Files
1. **`web.xml`** - Servlet registration
   - Location: `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/`
   - Added: Servlet definition and mapping to `/led` endpoint

2. **`index.html`** - Game file with LED integration
   - Location: `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/pages/`
   - Updated: LED controller to call servlet endpoint

3. **`index.html.template`** - Template file (prevents boot-time overwrite)
   - Location: Same as index.html
   - Updated: Same changes as index.html

## Quick Deployment Steps

### 1. Compile Servlet (Windows)
```cmd
compile_servlet.bat
```

### 2. Compile Servlet (Linux/Mac)
```bash
chmod +x compile_servlet.sh
./compile_servlet.sh
```

### 3. Manual Compilation (if scripts fail)
```bash
cd "partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes"
javac -cp "../../../lib/servlet-api-3.1.jar" LEDControlServlet.java
```

### 4. Deploy to Device
See `DEPLOYMENT_GUIDE.md` for complete instructions on:
- Creating partition images
- Regenerating MD5 checksums
- Writing to SD card
- Booting device

## How It Works

```mermaid
graph LR
    A[JavaScript Game] -->|HTTP GET| B[Jetty Server]
    B -->|Route /led| C[LEDControlServlet]
    C -->|Write| D[/sys/class/leds/red_led/brightness]
    C -->|Write| E[/sys/class/leds/green_led/brightness]
    C -->|JSON Response| A
```

### API Endpoint

**URL**: `http://localhost/frontpanel/led`

**Method**: GET

**Parameters**:
- `color` (required): "red" or "green"
- `brightness` (required): 0-100

**Example Requests**:
```bash
# Turn on green LED at full brightness
curl "http://localhost/frontpanel/led?color=green&brightness=100"

# Set red LED to 50% brightness
curl "http://localhost/frontpanel/led?color=red&brightness=50"

# Turn off green LED
curl "http://localhost/frontpanel/led?color=green&brightness=0"
```

**Response Format**:
```json
{
  "status": "success",
  "color": "green",
  "brightness": 100
}
```

**Error Response**:
```json
{
  "status": "error",
  "message": "Invalid color. Must be 'red' or 'green'"
}
```

## LED Events in Game

The game automatically triggers LED feedback for:

| Event | LED | Pattern |
|-------|-----|---------|
| Level Complete | Green | Flash 3 times |
| Power-Up Collected | Green | Brief pulse (200ms) |
| Ball Lost | Red | Flash 2 times |
| Game Over | Red | Flash 5 times |

## Testing LED Control

### Test on Device
```bash
# SSH into device
ssh root@<device-ip>

# Test LEDs directly
echo 100 > /sys/class/leds/green_led/brightness
sleep 1
echo 0 > /sys/class/leds/green_led/brightness

# Test via servlet
curl "http://localhost/frontpanel/led?color=red&brightness=100"
```

### Test from Browser Console
```javascript
// Open browser console (F12) and run:
fetch('/frontpanel/led?color=green&brightness=100')
  .then(r => r.json())
  .then(console.log);
```

## Troubleshooting

### LEDs Don't Work

**Check 1: Is servlet compiled?**
```bash
ls -la partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes/LEDControlServlet.class
```

**Check 2: Is servlet registered?**
```bash
grep -A5 "ledControlServlet" partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/web.xml
```

**Check 3: Can LEDs be controlled directly?**
```bash
# On device:
echo 100 > /sys/class/leds/red_led/brightness
```

**Check 4: Is Jetty running?**
```bash
# On device:
ps aux | grep jetty
netstat -tlnp | grep 80
```

### Compilation Errors

**Error**: `javac: command not found`
- **Solution**: Install Java JDK (not just JRE)

**Error**: `package javax.servlet does not exist`
- **Solution**: Specify correct classpath to servlet-api JAR
- **Find JAR**: `find partitions/*/opt/jetty-9-4-11/lib -name "*servlet*.jar"`

### Permission Errors

**Error**: `Permission denied` when writing to LED
- **Solution**: Check file permissions on device
  ```bash
  ls -la /sys/class/leds/*/brightness
  chmod 666 /sys/class/leds/*/brightness
  ```

## File Locations Reference

```
partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/
├── opt/
│   └── jetty-9-4-11/
│       ├── lib/
│       │   └── servlet-api-3.1.jar          # Needed for compilation
│       └── webapps/
│           └── frontpanel/
│               ├── pages/
│               │   ├── index.html            # Game file (modified)
│               │   └── index.html.template   # Template (modified)
│               └── WEB-INF/
│                   ├── web.xml               # Servlet config (modified)
│                   └── classes/
│                       ├── LEDControlServlet.java   # Source (new)
│                       └── LEDControlServlet.class  # Compiled (new)
```

## Next Steps

1. ✅ Compile servlet using provided scripts
2. ✅ Verify all files are in correct locations
3. ⏭️ Follow `DEPLOYMENT_GUIDE.md` for full deployment
4. ⏭️ Test LED control after device boots
5. ⏭️ Play game and verify LED feedback works

## Additional Documentation

- **`DEPLOYMENT_GUIDE.md`** - Complete deployment instructions
- **`LED_CONTROL_IMPLEMENTATION.md`** - Technical implementation details
- **`analysis.md`** - RoomWizard firmware analysis
- **`BRICK_BREAKER_README.md`** - Game features and controls