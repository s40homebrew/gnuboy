/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <string.h>
#include "defs.h"
#include "pcm.h"
#include "sound.h"
#include "port.h"
#include "s40/audio.h"

/* Core output is unsigned 8-bit mono. Keep the queue small and never wait for
 * audio space on the input/emulation task. The SDK copies accepted frames. */
static byte samples[512];
/* Match the native player's 256-frame blocks. Retaining the short tail avoids
 * waking its consumer for fragments that cannot fill a playback block. */
static int16_t pending[1024];
static unsigned pending_count;
static uint32_t fraction;
static unsigned wanted = 1, opened, failed;
static int volume = 65;
static uint32_t accepted_frames, dropped_frames;
static int last_open;

int port_audio_wanted(void) { return wanted; }
void port_audio_enable(int enable) { wanted = enable != 0; }
int port_audio_volume(void) { return volume; }
void port_audio_set_volume(int level) {
    volume = level < 0 ? 0 : level > 100 ? 100 : level;
}
const char *port_audio_label(void) {
    if (!wanted) return "SOUND OFF";
    return last_open < 0 ? "SOUND UNAVAILABLE" : "SOUND ON";
}
int port_audio_result(void) { return last_open; }
void port_audio_stop(void) {
    pcm.buf = NULL;
    pcm.pos = 0;
    pending_count = 0;
    if (opened) s40_audio_close();
    opened = failed = 0;
}
int port_audio_start(void) {
    port_audio_stop();
    fraction = accepted_frames = dropped_frames = 0;
    if (!wanted) return 0;
    const s40_audio_format format = {11025, 1, volume, 100};
    last_open = s40_audio_open(&format);
    if (last_open < 0) return -1;
    opened = 1;
    pcm.hz = format.rate;
    pcm.stereo = 0;
    pcm.buf = samples;
    pcm.len = sizeof samples;
    return 0;
}
int pcm_submit(void) {
    unsigned count = pcm.pos;
    pcm.pos = 0;
    if (!opened || failed || !count) return 0;
    if (count > sizeof samples) { failed = 1; return 0; }
    /* Gnuboy rounds its mixer interval to an integer number of 2MHz ticks.
     * Match the device's exact sample rate instead of slowly filling its queue. */
    unsigned step = (unsigned)snd.rate * 11025u;
    for (unsigned i = 0; i < count; ++i) {
        fraction += step;
        if (fraction >= 2097152u) {
            fraction -= 2097152u;
            if (pending_count < sizeof pending / sizeof *pending)
                pending[pending_count++] = ((int)samples[i] - 128) * 256;
            else
                ++dropped_frames;
        }
    }
    unsigned offset = 0;
    while (pending_count - offset >= 256) {
        unsigned count = (pending_count - offset) & ~255u;
        if (count > 512) count = 512;
        int written = s40_audio_write(pending + offset, count);
        if (written < 0 || (unsigned)written > count) { failed = 1; break; }
        if (!written) break;
        offset += (unsigned)written;
    }
    accepted_frames += offset;
    if (offset) {
        pending_count -= offset;
        memmove(pending, pending + offset, pending_count * sizeof *pending);
    }
    /* Do not clear pcm.buf here: sound_mix may append its next sample
     * immediately after this callback. Close only after the guest frame. */
    return 0;
}
int port_audio_flush(void) {
    pcm_submit();
    if (failed || (opened && s40_audio_status() < 0)) {
        last_open = -1;
        pcm.buf = NULL;
        pcm.pos = 0;
        return -1; /* Caller restores normal display before closing media. */
    }
    return 0;
}
void port_audio_stats(uint32_t *accepted, uint32_t *dropped) {
    *accepted = accepted_frames;
    *dropped = dropped_frames;
}
