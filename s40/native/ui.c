/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include "s40/runtime.h"
#include "port.h"
/* Five columns, low seven bits are rows. Compact readable menu alphabet. */
static const unsigned char letters[][5] = {
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {65,65,127,65,65},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {3,4,120,4,3},{97,81,73,69,67},
 {62,81,73,69,62},{0,66,127,64,0},{98,81,73,73,70},{34,65,73,73,54},
 {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
 {54,73,73,73,54},{6,73,73,41,30}
};
static uint32_t *screen;
void ui_clear(void) {
    screen = s40_display_open();
    for (unsigned i = 0; i < s40_display_width() * s40_display_height(); ++i) screen[i] = 0xff101820;
}
void ui_text(int x, int y, const char *text, uint32_t color, int scale) {
    if (!screen) return;
    unsigned width = s40_display_width(), height = s40_display_height();
    while (*text) {
        unsigned char c = *text++;
        unsigned char punctuation[5] = {0};
        if (c >= 'a' && c <= 'z') c -= 32;
        const unsigned char *glyph = punctuation;
        if (c >= 'A' && c <= 'Z') glyph = letters[c - 'A'];
        else if (c >= '0' && c <= '9') glyph = letters[26 + c - '0'];
        else if (c == '.') punctuation[2] = 64;
        else if (c == ':') punctuation[2] = 36;
        else if (c == '-') punctuation[0] = punctuation[1] = punctuation[2] = punctuation[3] = punctuation[4] = 8;
        else if (c == '+') { punctuation[0] = punctuation[1] = punctuation[3] = punctuation[4] = 8; punctuation[2] = 62; }
        else if (c == '%') { punctuation[0] = 35; punctuation[1] = 19; punctuation[2] = 8; punctuation[3] = 100; punctuation[4] = 98; }
        else if (c == '_') punctuation[0] = punctuation[1] = punctuation[2] = punctuation[3] = punctuation[4] = 64;
        else if (c == '/') { punctuation[0] = 64; punctuation[1] = 32; punctuation[2] = 16; punctuation[3] = 8; punctuation[4] = 4; }
        else if (c == '>') { punctuation[1] = 65; punctuation[2] = 34; punctuation[3] = 28; }
        else if (c == '*') { punctuation[0] = 20; punctuation[1] = 8; punctuation[2] = 62; punctuation[3] = 8; punctuation[4] = 20; }
        else if (c == '#') { punctuation[0] = 20; punctuation[1] = 127; punctuation[2] = 20; punctuation[3] = 127; punctuation[4] = 20; }
        for (int col = 0; col < 5; ++col) for (int row = 0; row < 7; ++row) if (glyph[col] & (1u << row))
            for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx) {
                int px = x + col * scale + dx, py = y + row * scale + dy;
                if ((unsigned)px < width && (unsigned)py < height) screen[py * width + px] = color;
            }
        x += 6 * scale;
        if (x >= (int)width - 5 * scale) break;
    }
}
void ui_present(void) { s40_display_present(); }
