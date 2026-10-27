#include "eaglenest.h"

// HUD values are drawn exactly where the original RPSCR routine puts them
// (panel labels/boxes are already baked into panel.raw):
//   score   = 6 hex digits, bottom strip, x = 80..176 step 16, y = 192
//   hits    = 2 hex digits, right column box, x = 272..288, y = 24
//   bullets = 2 hex digits, right column box, x = 272..288, y = 48
//   keys    = 2 hex digits, right column box, x = 272..288, y = 72
//
// The original WPRT copies the *whole* glyph cell (index-0 pixels included, i.e.
// it draws opaquely). So a digit covers whatever art is behind it -- notably the
// "EAGLES - NEST" logo baked into the bottom strip of panel.raw. Clear the cell
// before drawing the glyph to reproduce that instead of blending with the art.
static void hud_digit(int x, int y, char c) {
    renderer_draw_rect(x, y, TILE_W, TILE_H, 0);
    renderer_draw_char(x, y, renderer_char_to_glyph(c), 0);
}

// True if the pixel (px,py) lies inside the rounded rectangle at (x,y,w,h)
// with corner radius r. Corners are tested against their inscribed circles.
static bool rrect_inside(int px, int py, int x, int y, int w, int h, int r) {
    if (px < x || px >= x + w || py < y || py >= y + h) return false;
    if (r <= 0) return true;
    int dx = 0, dy = 0;
    if (px < x + r)            dx = (x + r) - px;
    else if (px >= x + w - r)  dx = px - (x + w - 1 - r);
    if (py < y + r)            dy = (y + r) - py;
    else if (py >= y + h - r)  dy = py - (y + h - 1 - r);
    return dx * dx + dy * dy <= r * r;
}

// Draw a rounded rectangle ringed by a `bw`-pixel border of `border`. The
// interior is filled with `fill`, or left transparent when `fill` is negative
// (so callers can stack rings for a beveled frame). Pixels outside the shape
// are untouched, so the rounded corners reveal the scene behind the popup.
static void popup_rect(int x, int y, int w, int h, int r, int bw, int fill, u8 border) {
    int ir = r - bw;
    if (ir < 0) ir = 0;
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            if (!rrect_inside(px, py, x, y, w, h, r)) continue;
            bool inner = rrect_inside(px, py, x + bw, y + bw, w - 2 * bw, h - 2 * bw, ir);
            if (inner) {
                if (fill < 0) continue;
                g_framebuffer.pixels[py][px] = palette_get((u8)fill);
            } else {
                g_framebuffer.pixels[py][px] = palette_get(border);
            }
        }
    }
}

void hud_draw(void) {
    char buf[16];

    snprintf(buf, sizeof(buf), "%06u", (unsigned)g_player.score);
    for (int i = 0; i < 6 && buf[i]; i++) {
        hud_digit(80 + i * TILE_W, 192, buf[i]);
    }

    snprintf(buf, sizeof(buf), "%02u", (unsigned)g_player.hits);
    hud_digit(272, 24, buf[0]);
    hud_digit(288, 24, buf[1]);

    snprintf(buf, sizeof(buf), "%02u", (unsigned)g_player.bullets);
    hud_digit(272, 48, buf[0]);
    hud_digit(288, 48, buf[1]);

    snprintf(buf, sizeof(buf), "%02u", (unsigned)g_player.keys);
    hud_digit(272, 72, buf[0]);
    hud_digit(288, 72, buf[1]);

    // Elevator pass pictogram (deviation from the original, which only prints a
    // one-off message): once the player carries the pass it is easy to forget,
    // so show its tile under the keys readout for as long as it is held.
    if (g_player.has_pass) {
        renderer_draw_tile(280, 88, tiles_get(0x6B), 0);
    }

    if (g_game.paused) {
        static const char* items[4] = {
            "R - RESUME", "L - RESTART LEVEL", "M - MAIN", "Q - QUIT"
        };

        // Longest label ("L - RESTART LEVEL", 17 chars) sizes the panel.
        const int w = 17 * TILE_W + 16;
        const int h = 100;
        const int r = 12;
        const int x = (SCREEN_W - w) / 2;
        const int y = (SCREEN_H - h) / 2;

        popup_rect(x + 5, y + 5, w, h, r, 3, 0, 0);   // drop shadow
        popup_rect(x, y, w, h, r, 3, 9, 2);           // dark panel, light frame
        popup_rect(x + 1, y + 1, w - 2, h - 2, r - 1, 1, -1, 1);  // inner bevel
        renderer_draw_rect(x + 16, y + 24, w - 32, 1, 1);

        renderer_draw_string(x + (w - 6 * TILE_W) / 2, y + 10, "PAUSED", 0);
        for (int i = 0; i < 4; i++) {
            renderer_draw_string(x + 8, y + 34 + i * 16, items[i], 0);
        }
    }
}