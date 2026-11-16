/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Port-local states contain no host pointers and are checked before mutation. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "defs.h"
#include "cpu.h"
#include "hw.h"
#include "mem.h"
#include "lcd.h"
#include "rtc.h"
#include "sound.h"
#include "port.h"

struct snapshot {
    uint32_t magic, version, size, rom_hash, checksum;
    struct cpu cpu;
    struct hw hw;
    struct lcd lcd;
    struct rtc rtc;
    struct snd snd;
    byte high[256], internal[8][4096];
    int model, rombank, rambank, enableram;
};
static uint32_t checksum(const void *data, size_t size) {
    uint32_t h = 2166136261u;
    const byte *p = data;
    while (size--) h = (h ^ *p++) * 16777619u;
    return h;
}
void *state_capture(size_t *size) {
    *size = sizeof(struct snapshot) + 8192u * mbc.ramsize;
    struct snapshot *s = calloc(1, *size);
    if (!s) return NULL;
    s->magic = 0x31534e47; s->version = 1; s->size = *size; s->rom_hash = port_rom_hash;
    s->cpu = cpu; s->hw = hw; s->lcd = lcd; s->rtc = rtc; s->snd = snd;
    memcpy(s->high, ram.hi, sizeof ram.hi); memcpy(s->internal, ram.ibank, sizeof ram.ibank);
    s->model = mbc.model; s->rombank = mbc.rombank; s->rambank = mbc.rambank; s->enableram = mbc.enableram;
    memcpy(s + 1, ram.sbank, 8192u * mbc.ramsize);
    s->checksum = checksum((byte *)s + 20, *size - 20);
    return s;
}
int state_restore(const void *data, size_t size) {
    const struct snapshot *s = data;
    if (size != sizeof(*s) + 8192u * mbc.ramsize || s->magic != 0x31534e47 || s->version != 1 ||
        s->size != size || s->rom_hash != port_rom_hash || s->hw.cgb != hw.cgb ||
        s->checksum != checksum((const byte *)s + 20, size - 20)) return -1;
    if ((unsigned)s->cpu.speed > 1 || (unsigned)s->cpu.halt > 1 ||
        (unsigned)s->cpu.ime > 1 || (unsigned)s->cpu.ima > 1 ||
        s->rombank < 0 || s->rombank >= mbc.romsize || s->rambank < 0 || s->rambank >= mbc.ramsize)
        return -1;
    cpu = s->cpu; hw = s->hw; lcd = s->lcd; rtc = s->rtc; snd = s->snd;
    memcpy(ram.hi, s->high, sizeof ram.hi); memcpy(ram.ibank, s->internal, sizeof ram.ibank);
    mbc.model = s->model; mbc.rombank = s->rombank; mbc.rambank = s->rambank; mbc.enableram = s->enableram;
    memcpy(ram.sbank, s + 1, 8192u * mbc.ramsize);
    hw.pad = 0; pad_refresh();
    /* This format saves the complete APU state. Rebuilding it from registers
     * would restart envelopes and destroy the restored counters. */
    vram_dirty(); pal_dirty(); mem_updatemap();
    return 0;
}
int state_file(int load) {
    char path[PORT_PATH_MAX + 8];
    snprintf(path, sizeof path, "%s.gns", port_rom_path);
    size_t size = sizeof(struct snapshot) + 8192u * mbc.ramsize;
    void *data = load ? malloc(size) : state_capture(&size);
    if (!data) return -1;
    int result = -1;
    if (!load) result = port_write_file(path, data, size);
    else for (unsigned attempt = 0; attempt < 2 && result; ++attempt) {
        char source[PORT_PATH_MAX + 16];
        snprintf(source, sizeof source, "%s%s", path, attempt ? ".bak" : "");
        FILE *f = fopen(source, "rb");
        if (f) {
            if (fread(data, 1, size, f) == size && fgetc(f) == EOF && !ferror(f)) result = state_restore(data, size);
            fclose(f);
        }
    }
    free(data);
    snprintf(port_message, sizeof port_message, "%s %s", load ? "LOAD" : "SAVE", result ? "FAILED" : "OK");
    return result;
}
