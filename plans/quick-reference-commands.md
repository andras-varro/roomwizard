# RoomWizard Modification - Quick Reference Commands

## Prerequisites Check

```bash
# Verify you have required tools
which mount umount md5sum fsck.ext3 sudo

# Check you're in the correct directory
pwd
# Should show: /path/to/roomwizard

# List partition files
ls -la partitions/
```

## Step-by-Step Commands

### 1. Create Bouncing Ball HTML File

```bash
# Create the bouncing ball HTML file
cat > bouncing-ball.html << 'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Bouncing Ball</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            overflow: hidden;
        }
        body {
            background: #000;
            touch-action: none;
        }
        canvas {
            display: block;
            cursor: none;
        }
    </style>
</head>
<body>
    <canvas id="canvas"></canvas>
    <script>
        const canvas = document.getElementById('canvas');
        const ctx = canvas.getContext('2d');
        
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
        
        const ball = {
            x: canvas.width / 2,
            y: canvas.height / 2,
            radius: 30,
            dx: 5,
            dy: 5,
            color: '#00ff00'
        };
        
        window.addEventListener('resize', () => {
            canvas.width = window.innerWidth;
            canvas.height = window.innerHeight;
        });
        
        canvas.addEventListener('touchstart', (e) => {
            e.preventDefault();
            ball.dx = -ball.dx;
            ball.dy = -ball.dy;
            ball.color = `hsl(${Math.random() * 360}, 100%, 50%)`;
        });
        
        canvas.addEventListener('click', (e) => {
            ball.dx = -ball.dx;
            ball.dy = -ball.dy;
            ball.color = `hsl(${Math.random() * 360}, 100%, 50%)`;
        });
        
        function draw() {
            ctx.fillStyle = 'rgba(0, 0, 0, 0.1)';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            
            ctx.beginPath();
            ctx.arc(ball.x, ball.y, ball.radius, 0, Math.PI * 2);
            ctx.fillStyle = ball.color;
            ctx.fill();
            ctx.closePath();
            
            ball.x += ball.dx;
            ball.y += ball.dy;
            
            if (ball.x + ball.radius > canvas.width || ball.x - ball.radius < 0) {
                ball.dx = -ball.dx;
            }
            if (ball.y + ball.radius > canvas.height || ball.y - ball.radius < 0) {
                ball.dy = -ball.dy;
            }
            
            requestAnimationFrame(draw);
        }
        
        draw();
        
        setInterval(() => {
            console.log('Bouncing ball active - watchdog fed via browser process');
        }, 30000);
    </script>
</body>
</html>
EOF

# Verify file was created
cat bouncing-ball.html | head -20
```

### 2. Find and Mount Root Filesystem

```bash
# Find the root filesystem image
find partitions/ -name "*.img" -type f

# Assuming it's named sd_rootfs_part.img or similar
ROOTFS_IMG="partitions/sd_rootfs_part.img"

# Create mount point
sudo mkdir -p /mnt/roomwizard_root

# Mount the image
sudo mount -o loop "$ROOTFS_IMG" /mnt/roomwizard_root

# Verify mount succeeded
mount | grep roomwizard
ls -la /mnt/roomwizard_root/opt/
```

### 3. Backup and Replace index.html

```bash
# Navigate to the webapp directory
cd /mnt/roomwizard_root/opt/jetty-9-4-11/webapps/frontpanel/pages/

# Backup original
sudo cp index.html index.html.original

# Verify backup
ls -la index.html*

# Copy new bouncing ball file
sudo cp /path/to/bouncing-ball.html index.html

# Set correct permissions
sudo chmod 644 index.html

# Verify the file
sudo head -20 index.html
```

### 4. Unmount Filesystem

```bash
# Return to project root
cd /path/to/roomwizard

# Sync all writes
sudo sync

# Unmount
sudo umount /mnt/roomwizard_root

# Verify unmounted
mount | grep roomwizard
# Should return nothing
```

### 5. Regenerate MD5 Checksum

```bash
# Navigate to partitions directory
cd partitions/

# Regenerate MD5 for modified image
md5sum sd_rootfs_part.img > sd_rootfs_part.img.md5

# Verify the checksum
cat sd_rootfs_part.img.md5

# Test validation
md5sum -c sd_rootfs_part.img.md5
# Should output: sd_rootfs_part.img: OK
```

### 6. Verify All MD5 Files

```bash
# List all MD5 files
ls -la *.md5

# Generate any missing MD5 files
for file in *.img *.gz *.bin; do
    if [ -f "$file" ] && [ ! -f "${file}.md5" ]; then
        md5sum "$file" > "${file}.md5"
        echo "Generated ${file}.md5"
    fi
done

# Verify all checksums
for md5file in *.md5; do
    echo "Checking $md5file..."
    md5sum -c "$md5file"
done
```

### 7. Final Verification

```bash
# Check filesystem integrity
sudo fsck.ext3 -n sd_rootfs_part.img

# Verify file sizes are reasonable
ls -lh *.img *.md5

# Create a manifest of changes
echo "Modified files:" > modification-manifest.txt
echo "- sd_rootfs_part.img (replaced index.html)" >> modification-manifest.txt
echo "- sd_rootfs_part.img.md5 (regenerated)" >> modification-manifest.txt
date >> modification-manifest.txt
```

## Troubleshooting Commands

### If Mount Fails

```bash
# Check if already mounted
mount | grep roomwizard

# Force unmount if stuck
sudo umount -f /mnt/roomwizard_root

# Check image file integrity
file sd_rootfs_part.img
# Should show: Linux ext3 filesystem data
```

### If MD5 Validation Fails

```bash
# Regenerate MD5
md5sum sd_rootfs_part.img > sd_rootfs_part.img.md5

# Compare with original (if you have it)
diff original_sd_rootfs_part.img.md5 sd_rootfs_part.img.md5
```

### If Filesystem Check Fails

```bash
# Run repair (CAUTION: Make backup first!)
sudo fsck.ext3 -y sd_rootfs_part.img
```

## Rollback Commands

### To Restore Original

```bash
# If you backed up the original image
cp sd_rootfs_part.img.backup sd_rootfs_part.img
md5sum sd_rootfs_part.img > sd_rootfs_part.img.md5
```

### To Restore Just index.html

```bash
# Mount filesystem
sudo mount -o loop sd_rootfs_part.img /mnt/roomwizard_root

# Restore from backup
sudo cp /mnt/roomwizard_root/opt/jetty-9-4-11/webapps/frontpanel/pages/index.html.original \
       /mnt/roomwizard_root/opt/jetty-9-4-11/webapps/frontpanel/pages/index.html

# Unmount and regenerate MD5
sudo umount /mnt/roomwizard_root
md5sum sd_rootfs_part.img > sd_rootfs_part.img.md5
```

## Writing Back to SD Card

**CAUTION**: Only do this after thorough testing!

```bash
# Identify SD card device (CAREFUL!)
lsblk
# Look for your SD card (e.g., /dev/sdb)

# Write partition image back
# Replace /dev/sdX5 with actual partition device
sudo dd if=partitions/sd_rootfs_part.img of=/dev/sdX5 bs=4M status=progress

# Sync
sudo sync

# Verify write
sudo md5sum /dev/sdX5 > /tmp/written.md5
# Compare with expected
diff partitions/sd_rootfs_part.img.md5 /tmp/written.md5
```

## Post-Deployment Monitoring

### Serial Console Connection

```bash
# Connect via screen
screen /dev/ttyUSB0 115200

# Or via minicom
minicom -D /dev/ttyUSB0 -b 115200
```

### Log Monitoring (if you have network access)

```bash
# SSH into device (if enabled)
ssh root@<roomwizard-ip>

# Monitor logs
tail -f /var/log/browser.out
tail -f /var/log/browser.err
tail -f /var/log/jettystart
tail -f /var/log/messages
```

## Safety Checklist

Before writing to SD card:

- [ ] Original SD card backed up
- [ ] Modified image MD5 regenerated
- [ ] All required MD5 files present
- [ ] Filesystem check passed (fsck)
- [ ] HTML file validated
- [ ] File permissions correct (644)
- [ ] Mount/unmount completed successfully
- [ ] No errors in any commands

## Quick Test in QEMU (Optional)

If you want to test before deploying to hardware:

```bash
# Install QEMU ARM
sudo apt-get install qemu-system-arm

# Extract kernel from boot partition
# (This is complex and may not work perfectly)
# Better to test directly on hardware with serial console access
```

## Emergency Recovery

If device won't boot after modification:

1. Remove SD card
2. Insert into computer
3. Mount root partition
4. Restore index.html.original
5. Regenerate MD5
6. Reinsert SD card
7. Device should boot normally

## Notes

- Always work on a copy of the partition images
- Keep original SD card as backup
- Test with serial console connected
- Monitor logs during first boot
- Have recovery plan ready