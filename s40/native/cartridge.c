/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

char port_message[128];
char port_rom_path[PORT_PATH_MAX];
uint32_t port_rom_hash;

static void save_path(char *path, const char *suffix) {
    snprintf(path, PORT_PATH_MAX + 8, "%s%s", port_rom_path, suffix);
}
static int write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == size;
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}
static int copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    if (!in) return -1;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return -1; }
    byte buffer[4096];
    size_t count;
    int result = 0;
    while ((count = fread(buffer, 1, sizeof buffer, in))) {
        if (fwrite(buffer, 1, count, out) != count) { result = -1; break; }
    }
    if (ferror(in)) result = -1;
    fclose(in);
    if (fclose(out)) result = -1;
    return result;
}
int port_write_file(const char *path, const void *data, size_t size) {
    char temporary[PORT_PATH_MAX + 16], backup[PORT_PATH_MAX + 16];
    snprintf(temporary, sizeof temporary, "%s.tmp", path);
    snprintf(backup, sizeof backup, "%s.bak", path);
    if (write_file(temporary, data, size)) return -1;
    FILE *old = fopen(path, "rb");
    if (old) {
        fclose(old);
        if (copy_file(path, backup)) return -1;
    }
    /* SDK rename is a checked copy, not an atomic filesystem operation.
     * Retain .bak even on success, and keep .tmp when the final copy fails. */
    return rename(temporary, path);
}
static int read_exact(const char *path, void *data, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int result = -1;
    if (!fseek(f, 0, SEEK_END) && ftell(f) == (long)size && !fseek(f, 0, SEEK_SET))
        result = fread(data, 1, size, f) == size ? 0 : -1;
    fclose(f);
    return result;
}
static void read_file(const char *path, void *data, size_t size) {
    char backup[PORT_PATH_MAX + 16];
    if (read_exact(path, data, size)) {
        snprintf(backup, sizeof backup, "%s.bak", path);
        read_exact(backup, data, size);
    }
}
int cartridge_save(void) {
    char path[PORT_PATH_MAX + 8];
    int result = 0;
    if (!rom.bank || !ram.loaded) return 0;
    if (mbc.batt) {
        save_path(path, ".sav");
        result = port_write_file(path, ram.sbank, 8192u * mbc.ramsize);
    }
    if (rtc.batt) {
        save_path(path, ".rtc");
        if (port_write_file(path, &rtc, sizeof rtc)) result = -1;
    }
    if (result) snprintf(port_message, sizeof port_message, "SAVE FAILED: INTERNAL STORAGE");
    return result;
}
void cartridge_unload(void) {
    free(rom.bank); free(ram.sbank);
    memset(&rom, 0, sizeof rom);
    memset(&ram, 0, sizeof ram);
    memset(&mbc, 0, sizeof mbc);
    memset(&rtc, 0, sizeof rtc);
    port_rom_path[0] = 0;
}
void cartridge_reset(void) {
    /* Keep guest APU lengths/envelopes/status advancing even when the output
     * is muted or the selected phone has no compatible PCM driver. */
    pcm.hz = 11025;
    hw.hdma = 0;
    hw_reset(); lcd_reset(); cpu_reset(); mbc_reset(); sound_reset();
    REG(RI_BOOT) = 0xff;
    mem_updatemap();
}

int cartridge_load(const char *path) {
    byte header[0x150];
    unsigned rom_banks, ram_banks;
    int type, battery = 0, clock = 0;
    FILE *f;
    const char *error = "CANNOT OPEN ROM";
    if (!path || strlen(path) >= sizeof port_rom_path) goto fail;
    f = fopen(path, "rb");
    if (!f) goto fail;
    error = "TRUNCATED ROM HEADER";
    if (fread(header, 1, sizeof header, f) != sizeof header) goto close_fail;
    error = "UNSUPPORTED ROM SIZE";
    if (header[0x148] <= 8) rom_banks = 2u << header[0x148];
    else if (header[0x148] >= 0x52 && header[0x148] <= 0x54) rom_banks = 128;
    else goto close_fail;
    static const unsigned sizes[] = {1, 1, 1, 4, 16, 8};
    error = "UNSUPPORTED SAVE RAM SIZE";
    if (header[0x149] >= sizeof sizes / sizeof *sizes) goto close_fail;
    ram_banks = sizes[header[0x149]];
    error = "UNSUPPORTED CARTRIDGE TYPE";
    switch (header[0x147]) {
    case 0: case 8: case 9: type = MBC_NONE; battery = header[0x147] == 9; break;
    case 1: case 2: case 3: type = MBC_MBC1; battery = header[0x147] == 3; break;
    case 5: case 6: type = MBC_MBC2; battery = header[0x147] == 6; ram_banks = 1; break;
    case 0x0f: case 0x10: clock = 1; battery = 1; type = MBC_MBC3; break;
    case 0x11: case 0x12: case 0x13: type = MBC_MBC3; battery = header[0x147] == 0x13; break;
    case 0x19: case 0x1a: case 0x1b: type = MBC_MBC5; battery = header[0x147] == 0x1b; break;
    case 0x1c: case 0x1d: case 0x1e: type = MBC_RUMBLE; battery = header[0x147] == 0x1e; break;
    case 0xff: type = MBC_HUC1; battery = 1; break;
    default: goto close_fail;
    }
    if (fseek(f, 0, SEEK_END)) goto close_fail;
    long length = ftell(f);
    error = "ROM LENGTH DOES NOT MATCH HEADER";
    size_t allocated = rom_banks * 16384u;
    size_t expected = allocated;
    if (header[0x148] == 0x52) expected = 72u * 16384;
    if (header[0x148] == 0x53) expected = 80u * 16384;
    if (header[0x148] == 0x54) expected = 96u * 16384;
    if (length < 0 || (size_t)length != expected) goto close_fail;
    if (fseek(f, 0, SEEK_SET)) goto close_fail;
    rom.bank = malloc(allocated);
    ram.sbank = calloc(ram_banks, 8192);
    error = "NOT ENOUGH MEMORY FOR ROM";
    if (!rom.bank || !ram.sbank) goto free_fail;
    memset(rom.bank, 0xff, allocated);
    error = "ROM READ FAILED";
    if (fread(rom.bank, 1, expected, f) != expected) goto free_fail;
    fclose(f);
    mbc.type = type; mbc.batt = battery; mbc.romsize = rom_banks; mbc.ramsize = ram_banks;
    hw.cgb = (header[0x143] & 0x80) != 0; hw.gba = 0;
    memcpy(rom.name, header + 0x134, hw.cgb ? 11 : 16);
    rom.name[hw.cgb ? 11 : 16] = 0;
    strcpy(port_rom_path, path);
    port_rom_hash = 2166136261u;
    for (size_t i = 0; i < expected; ++i) port_rom_hash = (port_rom_hash ^ ((byte *)rom.bank)[i]) * 16777619u;
    cartridge_reset();
    char saved[PORT_PATH_MAX + 8];
    if (battery) { save_path(saved, ".sav"); read_file(saved, ram.sbank, 8192u * ram_banks); }
    if (clock) {
        save_path(saved, ".rtc"); read_file(saved, &rtc, sizeof rtc);
        rtc.d &= 511; rtc.h = (unsigned)rtc.h % 24; rtc.m = (unsigned)rtc.m % 60;
        rtc.s = (unsigned)rtc.s % 60; rtc.t = (unsigned)rtc.t % 60;
        rtc.stop &= 1; rtc.carry &= 1; rtc.sel = 0;
    }
    rtc.batt = clock;
    ram.loaded = 1;
    port_message[0] = 0;
    return 0;
free_fail:
    cartridge_unload();
close_fail:
    fclose(f);
fail:
    snprintf(port_message, sizeof port_message, "%s", error);
    return -1;
}
