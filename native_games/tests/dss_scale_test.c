/* Quick probe: OMAP3 DSS hardware scaler via omapfb ioctls */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/omapfb.h>

int main(void) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("open /dev/fb0"); return 1; }

    /* 1. Query capabilities */
    struct omapfb_caps caps;
    memset(&caps, 0, sizeof(caps));
    if (ioctl(fd, OMAPFB_GET_CAPS, &caps) < 0)
        perror("GET_CAPS");
    else {
        printf("caps.ctrl = 0x%08x\n", caps.ctrl);
        printf("  PLANE_SCALE  = %s\n", (caps.ctrl & OMAPFB_CAPS_PLANE_SCALE) ? "YES" : "no");
        printf("  WINDOW_SCALE = %s\n", (caps.ctrl & OMAPFB_CAPS_WINDOW_SCALE) ? "YES" : "no");
    }

    /* 2. Query current plane info */
    struct omapfb_plane_info pi;
    memset(&pi, 0, sizeof(pi));
    if (ioctl(fd, OMAPFB_QUERY_PLANE, &pi) < 0) {
        perror("QUERY_PLANE");
    } else {
        printf("\nCurrent plane info:\n");
        printf("  pos_x=%u pos_y=%u enabled=%u channel_out=%u\n",
               pi.pos_x, pi.pos_y, pi.enabled, pi.channel_out);
        printf("  out_width=%u out_height=%u\n", pi.out_width, pi.out_height);
    }

    /* 3. Try: set fb to 320x200 */
    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO");
        close(fd);
        return 1;
    }
    printf("\nCurrent fb: %ux%u @ %u bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    vinfo.xres = 320;
    vinfo.yres = 200;
    vinfo.xres_virtual = 320;
    vinfo.yres_virtual = 200;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOPUT_VSCREENINFO 320x200");
    } else {
        printf("Set fb to 320x200 OK\n");
    }

    /* 4. Set plane output to 800x480 (hardware scale) */
    memset(&pi, 0, sizeof(pi));
    if (ioctl(fd, OMAPFB_QUERY_PLANE, &pi) < 0) {
        perror("QUERY_PLANE after resize");
    } else {
        printf("\nPlane after resize:\n");
        printf("  pos_x=%u pos_y=%u enabled=%u\n", pi.pos_x, pi.pos_y, pi.enabled);
        printf("  out_width=%u out_height=%u\n", pi.out_width, pi.out_height);
    }

    pi.pos_x = 0;
    pi.pos_y = 0;
    pi.out_width = 800;
    pi.out_height = 480;
    pi.enabled = 1;
    if (ioctl(fd, OMAPFB_SETUP_PLANE, &pi) < 0) {
        perror("SETUP_PLANE 800x480");
        printf("  -> Hardware scaling NOT available via this path\n");
    } else {
        printf("SETUP_PLANE 800x480 OK -> HARDWARE SCALING ACTIVE!\n");
    }

    /* 5. Verify */
    memset(&pi, 0, sizeof(pi));
    if (ioctl(fd, OMAPFB_QUERY_PLANE, &pi) == 0) {
        printf("\nFinal plane info:\n");
        printf("  pos_x=%u pos_y=%u enabled=%u\n", pi.pos_x, pi.pos_y, pi.enabled);
        printf("  out_width=%u out_height=%u\n", pi.out_width, pi.out_height);
    }

    /* 6. Fill fb with a test pattern (red) so we can see if it's scaled */
    struct fb_fix_screeninfo finfo;
    ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    printf("\nFinal fb: %ux%u stride=%u\n", vinfo.xres, vinfo.yres, finfo.line_length);

    /* Write red pixels (RGB565 = 0xF800) */
    unsigned short *fb = mmap(0, finfo.line_length * vinfo.yres, PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb != MAP_FAILED) {
        for (unsigned i = 0; i < vinfo.xres * vinfo.yres; i++)
            fb[i] = 0xF800; /* red */
        printf("Wrote red test pattern to 320x200 fb\n");
        printf("If screen shows full-screen red = hardware scaling works!\n");
        printf("Sleeping 3s...\n");
        sleep(3);
        munmap(fb, finfo.line_length * vinfo.yres);
    }

    /* 7. Restore 800x480 */
    pi.out_width = 0;
    pi.out_height = 0;
    ioctl(fd, OMAPFB_SETUP_PLANE, &pi);

    vinfo.xres = 800;
    vinfo.yres = 480;
    vinfo.xres_virtual = 800;
    vinfo.yres_virtual = 480;
    ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo);
    printf("Restored 800x480\n");

    close(fd);
    return 0;
}
