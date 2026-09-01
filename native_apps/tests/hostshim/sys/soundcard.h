/*
 * tests/hostshim/sys/soundcard.h — the one header that kept `common/audio.c`
 * off the host, and it needs no #ifdef in the shipped source to fix.
 *
 * This host has no `<sys/soundcard.h>` (that spelling is the BSD/OSS-proper one),
 * but it does ship `<linux/soundcard.h>`, and the kernel's copy defines every one
 * of the nine OSS symbols `common/audio.c` names: SNDCTL_DSP_{SPEED,STEREO,SETFMT,
 * RESET,GETOSPACE}, SOUND_PCM_READ_{RATE,BITS,CHANNELS}, AFMT_S16_LE, and
 * `struct audio_buf_info`.  So the shim is a redirect, not a mock — the constants
 * and the struct layout are the real ones.
 *
 * Put this directory FIRST on the include path (`-Itests/hostshim`) and
 * `common/audio.c` compiles unmodified on host gcc.  Nothing here is deployed and
 * nothing in `common/` knows it exists; `common/audio_out.c` solves the same
 * problem the other way, with an `__has_include` split (audio_out.c:465-478),
 * because it must also *drop* the device code.  audio.c must keep it — the tests
 * that use this shim never open /dev/dsp, they build an `Audio` by hand.
 */
#ifndef ROOMWIZARD_HOSTSHIM_SYS_SOUNDCARD_H
#define ROOMWIZARD_HOSTSHIM_SYS_SOUNDCARD_H
#include <linux/soundcard.h>
#endif
