/*
 * audio_wav.c — streaming mono RIFF/WAVE reader.  Rationale in audio_wav.h.
 */

#include "audio_wav.h"

#include <stdlib.h>
#include <string.h>

/* One frame of the file, in bytes.  16-bit only, checked at open. */
#define WAV_BYTES_PER_SAMPLE 2

/* Frames pulled from stdio per read() batch.  Bounded so a caller asking for a
 * huge block cannot put an unbounded array on the stack. */
#define WAV_BATCH_FRAMES 1024

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned int le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

bool audio_wav_open(AudioWav *w, const char *path, bool loop)
{
    if (!w || !path) return false;
    memset(w, 0, sizeof(*w));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "audio_wav: cannot open %s\n", path);
        return false;
    }

    char riff[12];
    if (fread(riff, 1, 12, f) != 12
        || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) {
        fprintf(stderr, "audio_wav: %s is not a RIFF/WAVE file\n", path);
        fclose(f);
        return false;
    }

    int  channels = 0, bits = 0, rate = 0;
    long data_pos = 0, data_bytes = 0;
    unsigned char hdr[8];

    /* ⚠️ The walk itself.  `fmt ` and `data` can arrive in any order and with
     * anything in between — LIST/INFO on our music, nothing on the vendor
     * effects — so every unknown chunk is SKIPPED by its declared size, with the
     * RIFF odd-size pad byte.  Dropping that pad desynchronises the walk on any
     * file with an odd-length chunk and the next "chunk ID" read is then two
     * bytes of somebody else's payload. */
    while (fread(hdr, 1, 8, f) == 8) {
        unsigned long sz = le32(hdr + 4);

        if (!memcmp(hdr, "fmt ", 4)) {
            unsigned char fmt[16];
            if (sz < 16 || fread(fmt, 1, 16, f) != 16) break;
            channels = (int)le16(fmt + 2);
            rate     = (int)le32(fmt + 4);
            bits     = (int)le16(fmt + 14);
            if (sz > 16) {
                if (fseek(f, (long)(sz - 16) + ((long)sz & 1), SEEK_CUR) != 0) break;
            }
        } else if (!memcmp(hdr, "data", 4)) {
            data_pos   = ftell(f);
            data_bytes = (long)sz;
            break;
        } else {
            if (fseek(f, (long)sz + ((long)sz & 1), SEEK_CUR) != 0) break;
        }
    }

    if (channels < 1 || bits != 16 || data_pos <= 0 || data_bytes <= 0) {
        fprintf(stderr, "audio_wav: %s: need 16-bit PCM with a data chunk "
                        "(got %d ch, %d bits, %ld data bytes at %ld)\n",
                path, channels, bits, data_bytes, data_pos);
        fclose(f);
        return false;
    }

    /* Believe the FILE over the header.  A size that over-claims — truncated
     * file, or a placeholder written by a streaming encoder — would otherwise
     * make `frames` promise audio that is not there, and the mix bus would keep
     * a voice alive rendering short fills to the end of a length nobody has. */
    if (fseek(f, 0, SEEK_END) == 0) {
        long end = ftell(f);
        if (end > data_pos && (end - data_pos) < data_bytes) {
            fprintf(stderr, "audio_wav: %s: data chunk claims %ld bytes, file "
                            "holds %ld — using the file\n",
                    path, data_bytes, end - data_pos);
            data_bytes = end - data_pos;
        }
    }

    long frame_bytes = (long)WAV_BYTES_PER_SAMPLE * channels;
    long frames      = data_bytes / frame_bytes;
    if (frames <= 0) {
        fprintf(stderr, "audio_wav: %s holds no whole frames\n", path);
        fclose(f);
        return false;
    }

    w->f          = f;
    w->rate       = rate;
    w->channels   = channels;
    w->data_pos   = data_pos;
    w->data_bytes = frames * frame_bytes;   /* whole frames only */
    w->frames     = frames;
    w->pos        = 0;
    w->loop       = loop;
    w->loops      = 0;

    if (fseek(f, data_pos, SEEK_SET) != 0) {
        fprintf(stderr, "audio_wav: %s: cannot seek to data at %ld\n", path, data_pos);
        audio_wav_close(w);
        return false;
    }
    return true;
}

void audio_wav_rewind(AudioWav *w)
{
    if (!w || !w->f) return;
    if (fseek(w->f, w->data_pos, SEEK_SET) == 0) w->pos = 0;
}

void audio_wav_close(AudioWav *w)
{
    if (!w) return;
    if (w->f) fclose(w->f);
    w->f = NULL;
}

long audio_wav_read(AudioWav *w, int16_t *dst, long frames)
{
    if (!w || !w->f || !dst || frames <= 0) return 0;

    long done = 0;
    while (done < frames) {
        if (w->pos >= w->frames) {
            if (!w->loop) break;
            audio_wav_rewind(w);
            w->loops++;
            if (w->pos >= w->frames) break;     /* rewind failed — do not spin */
        }

        long want = frames - done;
        long left = w->frames - w->pos;
        if (want > left)             want = left;
        if (want > WAV_BATCH_FRAMES) want = WAV_BATCH_FRAMES;

        if (w->channels == 1) {
            /* The common case on this device, and it needs no scratch: the file
             * is already what the bus wants. */
            size_t got = fread(dst + done, WAV_BYTES_PER_SAMPLE, (size_t)want, w->f);
            if (got == 0) break;
            done   += (long)got;
            w->pos += (long)got;
            continue;
        }

        /* Multi-channel: downmix a frame at a time, straight into `dst`.  No
         * scratch buffer, because there is nothing to stage — one file frame
         * becomes exactly one bus frame. */
        long batch = want;
        long i;
        for (i = 0; i < batch; i++) {
            int16_t frame[2];
            if (fread(frame, WAV_BYTES_PER_SAMPLE, 2, w->f) != 2) break;
            if (w->channels > 2
                && fseek(w->f, (long)(w->channels - 2) * WAV_BYTES_PER_SAMPLE,
                         SEEK_CUR) != 0) break;
            /* (L+R)/2, not the left channel.  The speaker sums L and R, so an
             * average is what makes a downmix audibly match the original;
             * picking one channel silently drops whatever was panned away.
             * `>> 1` rather than `/ 2` — Cortex-A8 has no hardware divide, and
             * an arithmetic shift right is the floor division we want on both
             * signs. */
            dst[done + i] = (int16_t)(((int32_t)frame[0] + (int32_t)frame[1]) >> 1);
        }
        if (i <= 0) break;
        done   += i;
        w->pos += i;
    }

    return done;
}

long audio_wav_fill(void *ctx, int16_t *dst, long frames)
{
    return audio_wav_read((AudioWav *)ctx, dst, frames);
}
