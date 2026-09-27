#include "common/audio.h"
#include <stdio.h>

/* Device-only probe: the four canned sounds, one after another.  Each is a mix-bus
 * voice until something pumps it, and audio_close() drains only what is already in
 * the device — so every sound is held with audio_hold_serviced() for its length,
 * or this program would exit having played nothing. */
int main(void) {
    Audio a;
    if (audio_init(&a) != 0) { puts("FAIL: no audio"); return 1; }
    puts("beep...");    audio_beep(&a);    audio_hold_serviced(&a, 200);
    puts("blip...");    audio_blip(&a);    audio_hold_serviced(&a, 200);
    puts("success..."); audio_success(&a); audio_hold_serviced(&a, 600);
    puts("fail...");    audio_fail(&a);    audio_hold_serviced(&a, 800);
    audio_close(&a);
    puts("DONE");
    return 0;
}
