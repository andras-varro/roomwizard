# LED Control Implementation Guide for RoomWizard

## Problem
JavaScript running in the browser cannot directly access `/sys/class/leds/` filesystem due to browser security restrictions.

## Solution Options

### **Option 1: Java Servlet Backend (Recommended)**
Create a servlet in the existing Jetty server that JavaScript can call via HTTP requests.

#### Step 1: Create Java LED Controller Servlet

Create file: `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes/LEDControlServlet.java`

```java
import javax.servlet.*;
import javax.servlet.http.*;
import java.io.*;

public class LEDControlServlet extends HttpServlet {
    private static final String RED_LED = "/sys/class/leds/red_led/brightness";
    private static final String GREEN_LED = "/sys/class/leds/green_led/brightness";
    
    @Override
    protected void doGet(HttpServletRequest request, HttpServletResponse response) 
            throws ServletException, IOException {
        String color = request.getParameter("color");
        String brightness = request.getParameter("brightness");
        
        response.setContentType("application/json");
        PrintWriter out = response.getWriter();
        
        try {
            if (color == null || brightness == null) {
                out.println("{\"status\":\"error\",\"message\":\"Missing parameters\"}");
                return;
            }
            
            int value = Integer.parseInt(brightness);
            value = Math.max(0, Math.min(100, value));
            
            String ledPath = color.equals("red") ? RED_LED : 
                           color.equals("green") ? GREEN_LED : null;
            
            if (ledPath == null) {
                out.println("{\"status\":\"error\",\"message\":\"Invalid color\"}");
                return;
            }
            
            try (FileWriter writer = new FileWriter(ledPath)) {
                writer.write(String.valueOf(value));
            }
            
            out.println("{\"status\":\"success\",\"color\":\"" + color + 
                       "\",\"brightness\":" + value + "}");
        } catch (Exception e) {
            out.println("{\"status\":\"error\",\"message\":\"" + 
                       e.getMessage().replace("\"", "'") + "\"}");
        }
    }
}
```

#### Step 2: Register Servlet in web.xml

Add to: `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/web.xml`

```xml
<servlet>
    <servlet-name>LEDControl</servlet-name>
    <servlet-class>LEDControlServlet</servlet-class>
</servlet>
<servlet-mapping>
    <servlet-name>LEDControl</servlet-name>
    <url-pattern>/led</url-pattern>
</servlet-mapping>
```

#### Step 3: Update JavaScript LED Controller

Replace the LED controller in the HTML file with:

```javascript
const LEDController = {
    available: true,
    
    async setLED(color, brightness) {
        try {
            const response = await fetch(`/frontpanel/led?color=${color}&brightness=${brightness}`);
            const result = await response.json();
            if (result.status !== 'success') {
                console.log('LED control failed:', result.message);
            }
        } catch (e) {
            console.log('LED control error:', e);
        }
    },
    
    async flash(color, times = 3) {
        for (let i = 0; i < times; i++) {
            await this.setLED(color, 100);
            await new Promise(resolve => setTimeout(resolve, 100));
            await this.setLED(color, 0);
            await new Promise(resolve => setTimeout(resolve, 100));
        }
    },
    
    async levelComplete() {
        await this.flash('green', 3);
    },
    
    async powerUpCollected() {
        await this.setLED('green', 100);
        setTimeout(() => this.setLED('green', 0), 200);
    },
    
    async ballLost() {
        await this.flash('red', 2);
    },
    
    async gameOver() {
        await this.flash('red', 5);
    }
};
```

#### Step 4: Compile and Deploy

```bash
# Compile the servlet
cd partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes
javac -cp "../../lib/*:../lib/*" LEDControlServlet.java

# Restart Jetty (will happen on device reboot)
```

---

### **Option 2: Shell Script CGI (Simpler Alternative)**

Create a simple shell script that Jetty can execute via CGI.

#### Step 1: Create LED Control Script

Create file: `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/sbin/led_control.sh`

```bash
#!/bin/sh
# LED Control CGI Script

echo "Content-Type: application/json"
echo ""

# Parse query string
COLOR=$(echo "$QUERY_STRING" | sed -n 's/.*color=\([^&]*\).*/\1/p')
BRIGHTNESS=$(echo "$QUERY_STRING" | sed -n 's/.*brightness=\([^&]*\).*/\1/p')

if [ -z "$COLOR" ] || [ -z "$BRIGHTNESS" ]; then
    echo '{"status":"error","message":"Missing parameters"}'
    exit 1
fi

# Validate brightness (0-100)
if [ "$BRIGHTNESS" -lt 0 ] || [ "$BRIGHTNESS" -gt 100 ]; then
    BRIGHTNESS=0
fi

# Set LED
case "$COLOR" in
    red)
        LED_PATH="/sys/class/leds/red_led/brightness"
        ;;
    green)
        LED_PATH="/sys/class/leds/green_led/brightness"
        ;;
    *)
        echo '{"status":"error","message":"Invalid color"}'
        exit 1
        ;;
esac

if echo "$BRIGHTNESS" > "$LED_PATH" 2>/dev/null; then
    echo "{\"status\":\"success\",\"color\":\"$COLOR\",\"brightness\":$BRIGHTNESS}"
else
    echo '{"status":"error","message":"Failed to write to LED"}'
    exit 1
fi
```

Make it executable:
```bash
chmod +x partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/sbin/led_control.sh
```

#### Step 2: Configure Jetty for CGI

This requires enabling CGI support in Jetty, which may be complex. **Option 1 (Java Servlet) is recommended.**

---

### **Option 3: Modify Existing Java Application**

If the RoomWizard Java application is accessible, add LED control methods there and expose them via existing endpoints.

---

## **Testing LED Control**

Once implemented, test from command line:

```bash
# Test red LED
curl "http://localhost/frontpanel/led?color=red&brightness=100"

# Test green LED  
curl "http://localhost/frontpanel/led?color=green&brightness=50"

# Turn off
curl "http://localhost/frontpanel/led?color=red&brightness=0"
```

## **Security Considerations**

1. **Rate Limiting**: Add rate limiting to prevent LED abuse
2. **Authentication**: Consider requiring authentication for LED control
3. **Input Validation**: Already implemented (0-100 range, color validation)

## **Fallback Behavior**

The current JavaScript implementation gracefully handles LED control failures:
- Logs errors to console
- Continues game operation even if LED control fails
- No impact on gameplay if LEDs are unavailable

## **Summary**

**To make LED control work:**
1. Create a Java servlet (Option 1) or shell script (Option 2)
2. Register it with Jetty web server
3. Update JavaScript to call the endpoint via HTTP
4. Compile and deploy to the partition
5. Reboot the device

The JavaScript code is already written and ready - it just needs the backend endpoint to exist.