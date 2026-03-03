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

    /* Re-verify channels after SPEED — some ALSA OSS shims reset channels */
    int ch_after_speed = 0;
    ioctl(fd, SNDCTL_DSP_CHANNELS, &ch_after_speed);
    printf("CHANNELS after SPEED: %d%s\n", ch_after_speed,
           (ch_after_speed != ch2) ? " *** SPEED RESET CHANNELS!" : " (OK)");

    close(fd);

    /* Test 3: set SPEED first, then CHANNELS (safer order) */
    fd = open("/dev/dsp", O_WRONLY);
    fmt = AFMT_S16_LE;
    ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
    rate = 22050;
    ioctl(fd, SNDCTL_DSP_SPEED, &rate);
    printf("\nTest 3 (SPEED then CHANNELS):\n");
    printf("  SPEED: %d\n", rate);
    int ch3 = 2;
    rc = ioctl(fd, SNDCTL_DSP_CHANNELS, &ch3);
    printf("  CHANNELS(2): rc=%d ch=%d\n", rc, ch3);
    /* Verify it stuck */
    int ch3_verify = 0;
    ioctl(fd, SNDCTL_DSP_CHANNELS, &ch3_verify);
    printf("  CHANNELS verify: %d%s\n", ch3_verify,
           (ch3_verify != 2) ? " *** STILL BROKEN" : " (OK)");

    /* Test 4: mono (what ScummVM now uses) */
    close(fd);
    fd = open("/dev/dsp", O_WRONLY);
    fmt = AFMT_S16_LE;
    ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
    int ch4 = 1;
    rc = ioctl(fd, SNDCTL_DSP_CHANNELS, &ch4);
    rate = 22050;
    ioctl(fd, SNDCTL_DSP_SPEED, &rate);
    int ch4_verify = 0;
    ioctl(fd, SNDCTL_DSP_CHANNELS, &ch4_verify);
    printf("\nTest 4 (mono, as ScummVM uses):\n");
    printf("  CHANNELS(1): rc=%d ch=%d\n", rc, ch4);
    printf("  SPEED: %d\n", rate);
    printf("  CHANNELS after SPEED: %d%s\n", ch4_verify,
           (ch4_verify == 1) ? " (OK)" : " *** UNEXPECTED");

    close(fd);
    return 0;
}
