/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "defs.h"
#include "cpu.h"
#include "lcd.h"
#include "regs.h"
#include "rtc.h"
#include "sound.h"
#include "port.h"

/* One guest frame follows gnuboy's LCD-driven CPU stepping. Presentation and
 * wall-clock pacing are separate, so skipped phone pictures do not slow games. */
void port_frame(void) {
    lcd_begin();
    cpu_emulate(2280);
    while (R_LY > 0 && R_LY < 144) cpu_emulate(cpu.lcdc);
    rtc_tick();
    sound_mix();
    if (!(R_LCDC & 0x80)) cpu_emulate(32832);
    while (R_LY > 0) cpu_emulate(cpu.lcdc);
}
