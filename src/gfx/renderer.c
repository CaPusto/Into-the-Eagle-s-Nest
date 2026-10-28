#include "eaglenest.h"

int g_scroll_x = 0;
int g_scroll_y = 0;

void renderer_init(void) {
    memset(g_framebuffer.pixels, 0, sizeof(g_framebuffer.pixels));
    g_scroll_x = 0;
    g_scroll_y = 0;
}

void renderer_clear(u8 color_index) {
    u32 color = palette_get(color_index);
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            g_framebuffer.pixels[y][x] = color;
        }
    }
}

void renderer_set_scroll(int x, int y) {
    g_scroll_x = x;
    g_scroll_y = y;
}

void renderer_draw_tile(int screen_x, int screen_y, const Tile* tile, u8 palette_offset) {
    if (!tile) return;

    for (int ty = 0; ty < TILE_H; ty++) {
        int dy = screen_y + ty;
        if (dy < 0 || dy >= SCREEN_H) continue;

        for (int tx = 0; tx < TILE_W; tx++) {
            int dx = screen_x + tx;
            if (dx < 0 || dx >= SCREEN_W) continue;

            // Tile data: 4 planes × 16 bytes (8 rows × 2 bytes)
            // Each plane: 8 rows × 2 bytes per row
            // High byte = left 8 px (MSB-first), low byte = right 8 px
            int bit = 7 - (tx % 8);  // MSB first (bit 7 = leftmost)
            int byte_in_row = tx / 8;  // 0 = high byte (left), 1 = low byte (right)
            int byte_idx = 0 * 16 + ty * 2 + byte_in_row;
            
            uint8_t pixel = (tile->data[byte_idx] >> bit) & 1;
            pixel |= ((tile->data[byte_idx + 16] >> bit) & 1) << 1;
            pixel |= ((tile->data[byte_idx + 32] >> bit) & 1) << 2;
            pixel |= ((tile->data[byte_idx + 48] >> bit) & 1) << 3;

            if (pixel != 0) {
                g_framebuffer.pixels[dy][dx] = palette_get((pixel + palette_offset) & 0xF);
            }
        }
    }
}

u8 renderer_char_to_glyph(char c) {
    if (c >= 'A' && c <= 'Z') return (u8)(c - 'A' + 1);
    if (c >= '0' && c <= '9') return (u8)(c - '0' + 27);
    if (c >= 'a' && c <= 'z') return (u8)(c - 'a' + 1);
    if (c == '.') return 37;
    if (c == '>') return 38;
    if (c == '_') return 39;
    return 0;
}

void renderer_draw_char(int screen_x, int screen_y, u8 char_index, u8 palette_offset) {
    (void)palette_offset;  // original PCHRMP copies glyph pens verbatim: colors are baked in
    if (char_index >= 46) return;
    renderer_draw_tile(screen_x, screen_y, &g_font[char_index], 0);
}

void renderer_draw_string(int x, int y, const char* str, u8 palette_offset) {
    int cx = x;
    while (*str) {
        renderer_draw_char(cx, y, renderer_char_to_glyph(*str), palette_offset);
        cx += TILE_W;
        str++;
    }
}

static bool load_embedded_scr_pal(const char* stem, u8* scr_data, size_t scr_size, u16* pal_words, size_t pal_size, bool* got_pal) {
    char scr_name[64], pal_name[64];
    snprintf(scr_name, sizeof(scr_name), "%s.scr", stem);
    snprintf(pal_name, sizeof(pal_name), "%s.pal", stem);

    const EmbeddedAsset* ea = embedded_asset_find(scr_name);
    if (!ea) ea = embedded_asset_find_by_suffix(scr_name);
    if (ea && ea->size == scr_size) {
        memcpy(scr_data, ea->data, scr_size);
    } else {
        return false;
    }

    if (pal_words && pal_size) {
        ea = embedded_asset_find(pal_name);
        if (!ea) ea = embedded_asset_find_by_suffix(pal_name);
        if (ea && ea->size == pal_size) {
            memcpy(pal_words, ea->data, pal_size);
            if (got_pal) *got_pal = true;
            return true;
        }
        if (got_pal) *got_pal = false;
    }
    return true;
}

// Full-screen planar 320x200 image (4 planes, MSB-first, p0=pixel bit0).
// Screen carries its own 16-color palette.
bool screen_load_planar(const char* base_path, const char* stem, Screen* s, bool has_palette_header) {
    if (!s) return false;

    char scr[256], pal[256];
    snprintf(scr, sizeof(scr), "%s/%s.scr", base_path, stem);

    bool loaded_from_embedded = false;
    {
        u16 words[PALETTE_SIZE];
        bool got_pal = false;
        loaded_from_embedded = load_embedded_scr_pal(stem, s->data, PLANAR_SIZE, has_palette_header ? words : NULL, has_palette_header ? sizeof(words) : 0, &got_pal);
        if (loaded_from_embedded && has_palette_header && got_pal) {
            for (int i = 0; i < PALETTE_SIZE; i++) {
                u16 w = (u16)((words[i] << 8) | (words[i] >> 8));
                u8 r = ((w >> 8) & 0xF) * 17;
                u8 g = ((w >> 4) & 0xF) * 17;
                u8 b = (w & 0xF) * 17;
                s->palette[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
            return true;
        } else if (loaded_from_embedded && !has_palette_header) {
            for (int i = 0; i < PALETTE_SIZE; i++) {
                s->palette[i] = palette_get(i);
            }
            return true;
        }
    }

    SDL_IOStream* io = SDL_IOFromFile(scr, "rb");
    if (!io) return false;
    size_t got = SDL_ReadIO(io, s->data, PLANAR_SIZE);
    SDL_CloseIO(io);
    if (got != PLANAR_SIZE) return false;

    if (has_palette_header) {
        snprintf(pal, sizeof(pal), "%s/%s.pal", base_path, stem);
        io = SDL_IOFromFile(pal, "rb");
        if (!io) return false;
        u16 words[PALETTE_SIZE];
        got = SDL_ReadIO(io, words, sizeof(words));
        SDL_CloseIO(io);
        if (got != sizeof(words)) return false;

        for (int i = 0; i < PALETTE_SIZE; i++) {
            u16 w = (u16)((words[i] << 8) | (words[i] >> 8));  // .pal is big-endian amiga words
            u8 r = ((w >> 8) & 0xF) * 17;
            u8 g = ((w >> 4) & 0xF) * 17;
            u8 b = (w & 0xF) * 17;
            s->palette[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    } else {
        for (int i = 0; i < PALETTE_SIZE; i++) {
            s->palette[i] = palette_get(i);
        }
    }
    return true;
}

void screen_palette_apply(const Screen* s) {
    if (!s) return;
    for (int i = 0; i < PALETTE_SIZE; i++) {
        g_palette.colors[i] = s->palette[i];
    }
}

void renderer_blit_planar(const Screen* s, int x0, int y0, int x1, int y1) {
    if (!s) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PLANAR_W) x1 = PLANAR_W;
    if (y1 > PLANAR_H) y1 = PLANAR_H;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int idx = 0;
            for (int p = 0; p < 4; p++) {
                u8 b = s->data[p * 8000 + y * 40 + (x / 8)];
                if (b & (1 << (7 - (x % 8)))) idx |= (1 << p);
            }
            g_framebuffer.pixels[y][x] = s->palette[idx];
        }
    }
}

void renderer_draw_rect(int x, int y, int w, int h, u8 color_index) {
    u32 color = palette_get(color_index);
    for (int dy = 0; dy < h; dy++) {
        int sy = y + dy;
        if (sy < 0 || sy >= SCREEN_H) continue;
        for (int dx = 0; dx < w; dx++) {
            int sx = x + dx;
            if (sx < 0 || sx >= SCREEN_W) continue;
            g_framebuffer.pixels[sy][sx] = color;
        }
    }
}

// Filled circle (used for the menu cursor ball).
void renderer_draw_disc(int cx, int cy, int radius, u8 color_index) {
    u32 color = palette_get(color_index);
    int r2 = radius * radius + radius;  // +radius fattens it into a full ball
    for (int dy = -radius; dy <= radius; dy++) {
        int sy = cy + dy;
        if (sy < 0 || sy >= SCREEN_H) continue;
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > r2) continue;
            int sx = cx + dx;
            if (sx < 0 || sx >= SCREEN_W) continue;
            g_framebuffer.pixels[sy][sx] = color;
        }
    }
}
