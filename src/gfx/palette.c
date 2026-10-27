#include "eaglenest.h"

static const u16 amiga_palette_raw[PALETTE_SIZE] = {
    0x000, 0x444, 0x777, 0x330,
    0x760, 0x020, 0x040, 0x232,
    0x077, 0x110, 0x743, 0x773,
    0x017, 0x730, 0x300, 0x710
};

void palette_init(void) {
    palette_load_from_amiga(amiga_palette_raw, PALETTE_SIZE);
}

void palette_set_color(u8 index, u32 color) {
    if (index < PALETTE_SIZE) {
        g_palette.colors[index] = color;
    }
}

// Original STCLR (eagle.asm 1270) writes each COLS word to the Amiga colour
// register after `add.l d0,d0`, so every 4-bit component is doubled on load
// (0x760 shows as 0xEC0, 0x330 as 0x660, ...). Picture palettes (ms/scrn) are
// copied straight to $DFF180 and are NOT doubled; they go through
// screen_load_planar instead of this function.
void palette_load_from_amiga(const u16* amiga_colors, int count) {
    for (int i = 0; i < count && i < PALETTE_SIZE; i++) {
        u16 c = amiga_colors[i];
        int r = ((c >> 8) & 0xF) * 2;
        int g = ((c >> 4) & 0xF) * 2;
        int b = (c & 0xF) * 2;
        if (r > 15) r = 15;
        if (g > 15) g = 15;
        if (b > 15) b = 15;
        g_palette.colors[i] = 0xFF000000 | ((u32)(r * 17) << 16) | ((u32)(g * 17) << 8) | (u32)(b * 17);
    }
}

u32 palette_get(u8 index) {
    if (index < PALETTE_SIZE) return g_palette.colors[index];
    return 0xFF000000;
}

void palette_black(void) {
    for (int i = 0; i < PALETTE_SIZE; i++) {
        g_palette.colors[i] = 0xFF000000;
    }
}

void palette_white(void) {
    for (int i = 0; i < PALETTE_SIZE; i++) {
        g_palette.colors[i] = 0xFFFFFFFF;
    }
}