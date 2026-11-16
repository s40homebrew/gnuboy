/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "defs.h"
#include "cpu.h"
#include "fb.h"
#include "hw.h"
#include "lcd.h"
#include "mem.h"
#include "pcm.h"
#include "regs.h"
#include "rtc.h"
#include "sound.h"
#include "port.h"
#include "s40/runtime.h"
#include "s40/input.h"
#include "s40/frame_clock.h"
#include "s40/video.h"
#include "s40/audio.h"

struct fb fb;
struct pcm pcm;
static s40_input_state input;
static unsigned scale = S40_VIDEO_FIT;
static int video_ready;
static char roms[64][PORT_PATH_MAX];
static unsigned rom_count;
static struct {
    uint32_t start, frames, presented, down, up;
    int active;
} run_stats;
static const s40_input_binding controls[] = {
    {S40_KEYCODE_RIGHT, PAD_RIGHT}, {S40_KEYCODE_LEFT, PAD_LEFT},
    {S40_KEYCODE_UP, PAD_UP}, {S40_KEYCODE_DOWN, PAD_DOWN},
    {'1', PAD_A}, {'3', PAD_B}, {S40_KEYCODE_SELECT, PAD_A},
    {'5', PAD_START}, {'0', PAD_SELECT}, {'z', PAD_A}, {'x', PAD_B},
    {' ', PAD_A}, {S40_KEYCODE_RETURN, PAD_START}
};
void die(char *format, ...) {
    char text[128];
    va_list ap; va_start(ap, format); vsnprintf(text, sizeof text, format, ap); va_end(ap);
    s40_error("%s", text);
}
void vid_setpal(int i, int r, int g, int b) { (void)i; (void)r; (void)g; (void)b; }

static void open_video(void) {
    if (!video_ready) {
        fb.ptr = (byte *)s40_video_open(160, 144);
        if (!fb.ptr) s40_error("Game framebuffer allocation failed");
        fb.w = 160; fb.h = 144; fb.pitch = 320; fb.pelsize = 2;
        fb.cc[0].l = 11; fb.cc[0].r = 3;
        fb.cc[1].l = 5; fb.cc[1].r = 2;
        fb.cc[2].l = 0; fb.cc[2].r = 3;
        fb.enabled = 1; fb.delegate_scaling = 1;
        video_ready = 1;
    }
    s40_video_set_scale(scale);
    pal_dirty();
}
static void release_input(void) {
    s40_input_release_all(&input);
    hw.pad = 0;
    pad_refresh();
}
static int poll_key(int playing) {
    s40_event e;
    int command = 0;
    s40_input_begin_frame(&input);
    s40_pump();
    while (s40_poll_event(&e)) {
        s40_input_handle(&input, &e);
        if (playing && e.category == S40_EVENT_KEYBOARD) {
            if (e.type == S40_KEY_DOWN) ++run_stats.down;
            if (e.type == S40_KEY_UP) ++run_stats.up;
        }
        if (e.category == S40_EVENT_KEYBOARD && e.type == S40_KEY_DOWN) {
            int key = s40_event_key(&e);
            if (playing) {
                /* A gameplay press later in the same drained batch must not
                 * overwrite Pause. Never replay old commands in a new menu. */
                if (key == '*' || key == S40_KEYCODE_SOFT_RIGHT || key == S40_KEYCODE_SOFT_LEFT)
                    command = '*';
                else if (key == '#' && !command) command = key;
            } else if (!command) command = key;
        }
    }
    /* A complete tap may arrive in one phone event batch. Keep its press for
     * one guest frame even when the release has already been dequeued. */
    hw.pad = (input.held | input.pressed) & 255;
    pad_refresh();
    return command;
}
static void start_stats(void) {
    memset(&run_stats, 0, sizeof run_stats);
    run_stats.start = s40_uptime_ms();
    run_stats.active = 1;
}
static void write_video_stats(FILE *log) {
    s40_video_stats stats;
    s40_video_get_stats(&stats);
    fprintf(log, "video_session_presents=%u\nvideo_fast_keys=%u\nvideo_fast_audio=%u\nvideo_service_yields=%u\nvideo_message_yields=%u\nvideo_suspend_ms=%u\nvideo_longest_suspend_ms=%u\nvideo_fallback=%s\n",
        stats.presents, stats.fast_key_messages, stats.fast_audio_messages, stats.service_yields, stats.message_yields,
        stats.suspend_ms, stats.longest_suspend_ms, s40_video_fallback_reason());
}
static void save_stats(void) {
    if (!run_stats.active) return;
    run_stats.active = 0;
    uint32_t elapsed = s40_uptime_ms() - run_stats.start;
    uint32_t accepted, dropped;
    port_audio_stats(&accepted, &dropped);
    s40_video_suspend();
    port_audio_stop();
    FILE *log = fopen("Gnuboy-session.txt", "wb");
    if (!log) return;
    fprintf(log, "rom=%s\nelapsed_ms=%u\nemulated_frames=%u\npresented_frames=%u\nkey_down=%u\nkey_up=%u\nvideo=%s\naudio_requested=%d\naudio_result=%d\naudio_frames=%u\naudio_dropped=%u\n",
        port_rom_path, elapsed, run_stats.frames, run_stats.presented, run_stats.down, run_stats.up,
        s40_video_backend(), port_audio_wanted(), port_audio_result(), accepted, dropped);
    write_video_stats(log);
    fclose(log);
}
static void cleanup(void) {
    save_stats();
    s40_video_suspend();
    port_audio_stop();
    cartridge_save();
    cartridge_unload();
}

static void benchmark(void) {
    port_audio_stop();
    size_t size;
    void *saved = state_capture(&size);
    if (!saved) { strcpy(port_message, "BENCHMARK: NO MEMORY"); return; }
    const unsigned frames = 120;
    unsigned elapsed[2];
    int presentation_status = 0;
    char backend[64];
    char fallback[64];
    open_video(); release_input();
    for (unsigned pass = 0; pass < 2; ++pass) {
        state_restore(saved, size);
        uint32_t start = s40_uptime_ms();
        for (unsigned i = 0; i < frames; ++i) {
            s40_pump(); port_frame();
            if (pass && s40_video_present()) presentation_status = -1;
        }
        if (pass) {
            snprintf(backend, sizeof backend, "%s", s40_video_backend());
            snprintf(fallback, sizeof fallback, "%s", s40_video_fallback_reason());
            if (s40_video_wait()) presentation_status = -1; /* Last asynchronous transfer. */
        }
        elapsed[pass] = s40_uptime_ms() - start;
    }
    s40_video_suspend();
    state_restore(saved, size); free(saved);
    FILE *log = fopen("Gnuboy-bench.txt", "wb");
    if (log) {
        fprintf(log, "rom=%s\nmode=%s\nbackend=%s\nvideo_fallback=%s\nframes=%u\ncompute_ms=%u\npresent_ms=%u\npresented_frames=%u\npresentation_status=%d\naudio_output=off\napu_hz=%d\n", port_rom_path,
            hw.cgb ? "CGB" : "DMG", backend, fallback, frames, elapsed[0], elapsed[1], frames, presentation_status, pcm.hz);
        write_video_stats(log);
        fclose(log);
    }
    snprintf(port_message, sizeof port_message, "BENCH %u/%u MS FOR %u FRAMES", elapsed[0], elapsed[1], frames);
}

static int pause_menu(void) {
    save_stats();
    s40_video_suspend(); port_audio_stop(); release_input();
    if (!cartridge_save()) port_message[0] = 0;
    int dirty = 1;
    for (;;) {
        if (dirty) {
            ui_clear(); ui_text(8, 12, "GNUBOY - PAUSED", 0xff80d0ff, 2);
            ui_text(8, 42, rom.name, 0xffffffff, 1);
            ui_text(8, 66, "CENTER / LEFT: RESUME", 0xffffffff, 1);
            ui_text(8, 88, "1: SAVE STATE", 0xffffffff, 1);
            ui_text(8, 110, "3: LOAD STATE", 0xffffffff, 1);
            ui_text(8, 132, scale ? "#: SCALE - NATIVE" : "#: SCALE - FIT", 0xffffffff, 1);
            ui_text(8, 154, "9: SHORT BENCHMARK", 0xffffffff, 1);
            ui_text(8, 176, "0: CHOOSE ANOTHER ROM", 0xffffffff, 1);
            ui_text(8, 198, "RIGHT: SAVE AND EXIT", 0xffffffff, 1);
            char audio_line[40];
            snprintf(audio_line, sizeof audio_line, "7: %s %d%%", port_audio_label(), port_audio_volume());
            ui_text(8, 214, audio_line, 0xffa0b0c0, 1);
            ui_text(8, 226, "2/8: VOLUME +10 / -10", 0xffa0b0c0, 1);
            ui_text(8, 242, "PAD:MOVE 1/3:A/B 5/0:START/SEL", 0xffa0b0c0, 1);
            ui_text(8, 270, port_message, 0xffffd080, 1);
            ui_present(); dirty = 0;
        }
        int key = poll_key(0);
        if (key == S40_KEYCODE_SELECT || key == S40_KEYCODE_SOFT_LEFT || key == '*') { release_input(); return 0; }
        if (key == S40_KEYCODE_SOFT_RIGHT) { if (!cartridge_save()) return 2; dirty = 1; }
        if (key == '0') { if (!cartridge_save()) return 1; dirty = 1; }
        if (key == '1' || key == '3') { state_file(key == '3'); dirty = 1; }
        if (key == '#') { scale ^= 1; s40_video_set_scale(scale); dirty = 1; }
        if (key == '9') { benchmark(); dirty = 1; }
        if (key == '7') { port_audio_enable(!port_audio_wanted()); dirty = 1; }
        if (key == '2' || key == '8') {
            port_audio_set_volume(port_audio_volume() + (key == '2' ? 10 : -10));
            dirty = 1;
        }
        s40_sleep_ms(10);
    }
}
static int play(void) {
    s40_frame_clock clock;
    open_video(); release_input();
    port_audio_start();
    start_stats();
    s40_frame_clock_init(&clock, s40_uptime_ms(), 4194304, 70224);
    unsigned skipped = 0;
    for (;;) {
        int key = poll_key(1);
        if (key == '*' || key == S40_KEYCODE_SOFT_RIGHT || key == S40_KEYCODE_SOFT_LEFT) {
            int action = pause_menu();
            if (action) return action;
            open_video(); port_audio_start(); start_stats(); s40_frame_clock_reset(&clock, s40_uptime_ms());
        }
        if (key == '#') { scale ^= 1; s40_video_set_scale(scale); }
        s40_frame_clock_step(&clock);
        port_frame();
        ++run_stats.frames;
        if (port_audio_flush() < 0) {
            s40_video_suspend();
            port_audio_stop();
        }
        uint32_t now = s40_uptime_ms();
        /* Keep the guest's exact clock when a slower panel needs a skipped picture. */
        if ((int32_t)(now - clock.deadline) <= 0 || skipped >= 3) {
            s40_video_present(); ++run_stats.presented; skipped = 0;
        } else ++skipped;
        uint32_t delay = s40_frame_clock_delay(&clock, s40_uptime_ms());
        if (delay) s40_sleep_ms(delay);
        /* Recover from a long phone-service interruption without a catch-up storm. */
        if ((int32_t)(s40_uptime_ms() - clock.deadline) > 250) s40_frame_clock_reset(&clock, s40_uptime_ms());
    }
}
static int add_rom(const char *path) {
    static const char gallery[] = "C:/predefgallery/";
    if (!strncasecmp(path, gallery, sizeof gallery - 1)) path += sizeof gallery - 1;
    if (!path[0] || strlen(path) >= PORT_PATH_MAX) return -1;
    for (unsigned i = 0; i < rom_count; ++i) if (!strcmp(roms[i], path)) return i;
    if (rom_count >= 64) return -1;
    strcpy(roms[rom_count], path);
    return rom_count++;
}
static void read_rom_list(void) {
    FILE *f = fopen("gnuboy.txt", "rb");
    if (f) {
        char line[PORT_PATH_MAX + 4];
        while (fgets(line, sizeof line, f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = 0;
            if (n && line[0] != '#') add_rom(line);
        }
        fclose(f);
    }
    const char *defaults[] = {"tobu.gb", "tobudx.gb", "ucity.gbc"};
    for (unsigned i = 0; i < 3; ++i) {
        char path[PORT_PATH_MAX];
        snprintf(path, sizeof path, "predeffilereceived/%s", defaults[i]);
        f = fopen(path, "rb");
        if (f) { fclose(f); add_rom(path); }
        else {
            /* Keep previously installed root-level games usable natively. */
            f = fopen(defaults[i], "rb");
            if (f) { fclose(f); add_rom(defaults[i]); }
        }
    }
}
static int choose_rom(void) {
    unsigned selected = 0;
    int dirty = 1;
    release_input();
    for (;;) {
        if (dirty) {
            ui_clear(); ui_text(8, 12, "GNUBOY", 0xff80d0ff, 2);
            ui_text(8, 42, "GAME BOY / GAME BOY COLOR", 0xffa0b0c0, 1);
            if (!rom_count) {
                ui_text(8, 78, "NO GAMES SELECTED", 0xffffffff, 1);
                ui_text(8, 104, "USE SELECT ROM IN LAUNCHER", 0xffffffff, 1);
                ui_text(8, 126, "OR PUT FILENAMES IN GNUBOY.TXT", 0xffffffff, 1);
                ui_text(8, 148, "IN INTERNAL GALLERY", 0xffffffff, 1);
            }
            unsigned first = selected / 7 * 7;
            for (unsigned i = first; i < rom_count && i < first + 7; ++i) {
                const char *name = strrchr(roms[i], '/');
                ui_text(8, 72 + (i - first) * 22, i == selected ? ">" : " ", 0xff80d0ff, 1);
                ui_text(22, 72 + (i - first) * 22, name ? name + 1 : roms[i], i == selected ? 0xffffffff : 0xffa0b0c0, 1);
            }
            ui_text(8, 250, port_message, 0xffffd080, 1);
            ui_text(8, 286, "CENTER: PLAY   RIGHT: EXIT", 0xff80d0ff, 1);
            ui_present(); dirty = 0;
        }
        int key = poll_key(0);
        if (key == S40_KEYCODE_SOFT_RIGHT) return -1;
        if (rom_count && key == S40_KEYCODE_UP) { selected = selected ? selected - 1 : rom_count - 1; dirty = 1; }
        if (rom_count && key == S40_KEYCODE_DOWN) { selected = (selected + 1) % rom_count; dirty = 1; }
        if (rom_count && (key == S40_KEYCODE_SELECT || key == S40_KEYCODE_SOFT_LEFT || key == '5')) return selected;
        s40_sleep_ms(10);
    }
}
int s40_app_main(void) {
    s40_set_root("C:/predefgallery/");
    s40_display_keep_awake(1);
    s40_audio_bind_device();
    s40_input_state_init(&input, s40_input_layout(), controls, sizeof controls / sizeof *controls);
    s40_on_exit(cleanup, 0);
    read_rom_list();
    const char *argument = s40_launch_argument();
    int selected = -1;
    if (argument && *argument) {
        selected = add_rom(argument);
    }
    for (;;) {
        if (selected < 0) selected = choose_rom();
        if (selected < 0) return 0;
        open_video();
        if (cartridge_load(roms[selected])) { s40_video_suspend(); selected = -1; continue; }
        int action = play();
        cartridge_unload();
        if (action == 2) return 0;
        selected = -1;
    }
}
