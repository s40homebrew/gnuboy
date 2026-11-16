/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GNUBOY_S40_PORT_H
#define GNUBOY_S40_PORT_H
#include <stddef.h>
#include <stdint.h>
#define PORT_PATH_MAX 240
extern char port_message[128];
extern char port_rom_path[PORT_PATH_MAX];
extern uint32_t port_rom_hash;
int cartridge_load(const char *path);
void cartridge_unload(void);
int cartridge_save(void);
/* Stages a closed temporary file and retains the preceding file as .bak. */
int port_write_file(const char *path, const void *data, size_t size);
void cartridge_reset(void);
void port_frame(void);
int port_audio_wanted(void);
void port_audio_enable(int enable);
int port_audio_volume(void);
void port_audio_set_volume(int level);
const char *port_audio_label(void);
int port_audio_result(void);
int port_audio_start(void);
void port_audio_stop(void);
int port_audio_flush(void);
void port_audio_stats(uint32_t *accepted, uint32_t *dropped);
void *state_capture(size_t *size);
int state_restore(const void *data, size_t size);
int state_file(int load);
void ui_clear(void);
void ui_text(int x, int y, const char *text, uint32_t color, int scale);
void ui_present(void);
#endif
