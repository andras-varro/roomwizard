#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

int main(void) {
    int fd = open("/dev/dsp", O_WRONLY);
    if (fd < 0) { perror("open"); return 1; }

    int fmt = AFMT_S16_LE;
    ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
    printf("FMT: 0x%x\n", fmt);

    /* Test SNDCTL_DSP_STEREO (deprecated, used in our mixer) */
    int stereo = 1;
    int rc = ioctl(fd, SNDCTL_DSP_STEREO, &stereo);
    printf("SNDCTL_DSP_STEREO(1): rc=%d stereo=%d\n", rc, stereo);

    /* Check what CHANNELS reports now */
    int ch = 0;
    ioctl(fd, SNDCTL_DSP_CHANNELS, &ch);
    printf("CHANNELS after STEREO: %d\n", ch);

    int rate = 22050;
    ioctl(fd, SNDCTL_DSP_SPEED, &rate);
    printf("SPEED: requested 22050 got %d\n", rate);

    close(fd);

    /* Test 2: use SNDCTL_DSP_CHANNELS directly */
    fd = open("/dev/dsp", O_WRONLY);
    fmt = AFMT_S16_LE;
    ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
    int ch2 = 2;
    rc = ioctl(fd, SNDCTL_DSP_CHANNELS, &ch2);
    printf("SNDCTL_DSP_CHANNELS(2): rc=%d ch=%d\n", rc, ch2);
    rate = 22050;
    ioctl(fd, SNDCTL_DSP_SPEED, &rate);
    printf("SPEED after CHANNELS: %d\n", rate);
    close(fd);
    return 0;
}
