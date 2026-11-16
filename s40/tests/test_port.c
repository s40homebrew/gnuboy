/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "defs.h"
#include "cpu.h"
#include "fb.h"
#include "hw.h"
#include "lcd.h"
#include "mem.h"
#include "pcm.h"
#include "regs.h"
#include "sound.h"
#include "port.h"

struct fb fb;
struct pcm pcm;
static uint16_t pixels[160 * 144];
void vid_setpal(int i, int r, int g, int b) { (void)i; (void)r; (void)g; (void)b; }
int pcm_submit(void) { return 0; }
void die(char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); abort();
}
static void fixture(const char *name, unsigned mode, unsigned type, unsigned rom_size, unsigned ram_size) {
    unsigned char data[32768] = {0};
    data[0x100] = 0xc3; data[0x101] = 0x50; data[0x102] = 0x01;
    data[0x150] = 0x18; data[0x151] = 0xfe; /* JR -2: deterministic LCD/CPU clock exercise. */
    memcpy(data + 0x134, "S40 TEST", 8);
    data[0x143] = mode; data[0x147] = type; data[0x148] = rom_size; data[0x149] = ram_size;
    FILE *f = fopen(name, "wb"); assert(f); assert(fwrite(data, 1, sizeof data, f) == sizeof data); assert(!fclose(f));
}
int main(int argc, char **argv) {
    fb.ptr = (byte *)pixels; fb.w = 160; fb.h = 144; fb.pitch = 320; fb.pelsize = 2; fb.enabled = 1;
    fb.cc[0].l = 11; fb.cc[0].r = 3; fb.cc[1].l = 5; fb.cc[1].r = 2; fb.cc[2].r = 3;
    fixture("test.gb", 0, 3, 0, 2);
    assert(!cartridge_load("test.gb")); assert(!hw.cgb && mbc.romsize == 2 && mbc.batt && mbc.ramsize == 1);
    for (int i = 0; i < 5; ++i) port_frame();
    sound_write(RI_NR21, 0x3f); sound_write(RI_NR22, 0xf0); sound_write(RI_NR24, 0xc0);
    assert(snd.ch[1].on);
    port_frame();
    assert(!(sound_read(RI_NR52) & 2)); assert(cpu.snd < snd.rate);
    size_t length;
    ram.sbank[0][10] = 0x42;
    snd.ch[1].envol = 5; /* Evolved envelope differs from the NR22 initial value. */
    void *saved = state_capture(&length); assert(saved);
    ram.sbank[0][10] = 0x17; cpu.pc.d = 0;
    assert(!state_restore(saved, length)); assert(ram.sbank[0][10] == 0x42);
    assert(snd.ch[1].envol == 5);
    assert(state_restore(saved, length - 1) == -1);
    ((byte *)saved)[length - 1] ^= 1;
    assert(state_restore(saved, length) == -1);
    ((byte *)saved)[length - 1] ^= 1;
    uint32_t identity = port_rom_hash; port_rom_hash ^= 1;
    assert(state_restore(saved, length) == -1); port_rom_hash = identity;
    free(saved);
    assert(!cartridge_save()); ram.sbank[0][10] = 0x23; assert(!cartridge_save());
    FILE *backup = fopen("test.gb.sav.bak", "rb"); assert(backup); assert(!fseek(backup, 10, SEEK_SET)); assert(fgetc(backup) == 0x42); fclose(backup);
    assert(!state_file(0)); ram.sbank[0][10] = 0; assert(!state_file(1)); assert(ram.sbank[0][10] == 0x23);
    cartridge_unload();
    assert(!cartridge_load("test.gb")); assert(ram.sbank[0][10] == 0x23); cartridge_unload();
    fixture("test.gbc", 0x80, 0x1b, 0, 4);
    assert(!cartridge_load("test.gbc")); assert(hw.cgb && mbc.ramsize == 16);
    hw.hdma = 0x81; cartridge_reset(); assert(hw.hdma == 0 && hw.cgb);
    for (int i = 0; i < 5; ++i) port_frame();
    saved = state_capture(&length); assert(saved && length > 128 * 1024); assert(!state_restore(saved, length)); free(saved); cartridge_unload();
    fixture("invalid.gb", 0, 0, 1, 0); assert(cartridge_load("invalid.gb") == -1); assert(!rom.bank);
    fixture("invalid.gb", 0, 0x22, 0, 0); assert(cartridge_load("invalid.gb") == -1); assert(!rom.bank);
    for (int arg = 1; arg < argc; ++arg) {
        assert(!cartridge_load(argv[arg]));
        for (int i = 0; i < 120; ++i) port_frame();
        unsigned hash = 2166136261u;
        for (unsigned i = 0; i < 160 * 144; ++i) hash = (hash ^ pixels[i]) * 16777619u;
        printf("120 frames: %s (%s), pixels %08x\n", rom.name, hw.cgb ? "CGB" : "DMG", hash);
        cartridge_unload();
    }
    puts("ROM loading, GB/GBC execution, state validation, and save backups passed.");
    return 0;
}
