/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "defs.h"
#include "pcm.h"
#include "sound.h"
#include "sys.h"
#include "port.h"
#include "s40/audio.h"

struct pcm pcm;
struct snd snd;
static int open_result = 1, write_error, stream_open;
static unsigned write_calls, received, capacity = 8192, close_calls;
static unsigned opened_volume;
static int16_t output[8192];
int s40_audio_open(const s40_audio_format *f) {
    assert(f->rate == 11025 && f->channels == 1 && f->buffer_ms == 100);
    opened_volume = f->volume;
    stream_open = open_result >= 0;
    return open_result;
}
int s40_audio_write(const int16_t *samples, unsigned frames) {
    ++write_calls;
    assert(frames <= 512 && frames % 256 == 0);
    if (write_error) return -1;
    unsigned count = frames < capacity ? frames : capacity;
    assert(received + count <= sizeof output / sizeof *output);
    memcpy(output + received, samples, count * sizeof *output);
    received += count; capacity -= count;
    return count;
}
int s40_audio_status(void) { return stream_open; }
void s40_audio_close(void) { stream_open = 0; ++close_calls; }
static void begin(void) {
    port_audio_stop();
    received = write_calls = close_calls = 0; capacity = 8192; write_error = 0; open_result = 1;
    snd.rate = 190;
    port_audio_enable(1);
    assert(!port_audio_start());
    assert(pcm.buf && pcm.len == 512 && !pcm.stereo);
}
int main(void) {
    begin();
    assert(opened_volume == 65);
    pcm.buf[0] = 128; pcm.buf[1] = 0; pcm.buf[2] = 255; pcm.pos = 3;
    port_audio_flush();
    assert(received == 0 && write_calls == 0);
    memset(pcm.buf, 128, 254); pcm.pos = 254; port_audio_flush();
    assert(received == 256 && output[0] == -32768 && output[1] == 32512);
    assert(pcm.pos == 0);
    begin();
    for (unsigned i = 0; i < 10; ++i) {
        memset(pcm.buf, 128, 512); pcm.pos = 512; port_audio_flush();
    }
    assert(received == ((uint64_t)5120 * 190 * 11025 / 2097152 / 256) * 256);
    for (unsigned i = 0; i < received; ++i) assert(output[i] == 0);
    begin(); capacity = 17;
    memset(pcm.buf, 128, 512); pcm.pos = 512; port_audio_flush();
    uint32_t accepted, dropped; port_audio_stats(&accepted, &dropped);
    assert(accepted == 17 && dropped == 0 && write_calls == 2);
    for (unsigned i = 0; i < 3; ++i) {
        pcm.pos = 512; port_audio_flush();
    }
    port_audio_stats(&accepted, &dropped);
    unsigned produced = (uint64_t)2048 * 190 * 11025 / 2097152;
    assert(accepted == 17 && dropped == produced - 17 - 1024 && write_calls == 5);
    capacity = 8192; pcm.pos = 1; port_audio_flush();
    port_audio_stats(&accepted, &dropped);
    assert(accepted == 1041 && dropped == produced - 17 - 1024 + 1);
    begin();
    for (unsigned i = 0; i < 20; ++i) {
        memset(pcm.buf, 128, 185); pcm.pos = 185; port_audio_flush();
    }
    assert(write_calls == received / 256 && write_calls < 20);
    begin(); write_error = 1;
    pcm.pos = 512; pcm_submit();
    assert(pcm.buf); /* The core may append immediately after pcm_submit. */
    assert(port_audio_flush() < 0);
    assert(!pcm.buf && close_calls == 0 && !strcmp(port_audio_label(), "SOUND UNAVAILABLE"));
    port_audio_stop(); assert(close_calls == 1);
    open_result = -50; assert(port_audio_start() == -1 && !pcm.buf);
    port_audio_enable(0); assert(!port_audio_start() && !pcm.buf);
    assert(!strcmp(port_audio_label(), "SOUND OFF"));
    open_result = 1; port_audio_enable(1);
    port_audio_set_volume(-10); assert(!port_audio_start());
    assert(port_audio_volume() == 0 && opened_volume == 0);
    port_audio_set_volume(110); assert(!port_audio_start());
    assert(port_audio_volume() == 100 && opened_volume == 100);
    port_audio_set_volume(45); assert(!port_audio_start());
    assert(port_audio_volume() == 45 && opened_volume == 45);
    port_audio_stop();
    puts("PCM conversion, exact-rate sampling, bounded backpressure and audio failure cleanup passed.");
    return 0;
}
