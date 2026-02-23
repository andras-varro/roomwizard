#include "common/audio.h"
#include <stdio.h>

int main(void) {
    Audio a;
    if (audio_init(&a) != 0) { puts("FAIL: no audio"); return 1; }
    puts("beep...");    audio_beep(&a);
    puts("blip...");    audio_blip(&a);
    puts("success..."); audio_success(&a);
    puts("fail...");    audio_fail(&a);
    audio_close(&a);
    puts("DONE");
    return 0;
}
