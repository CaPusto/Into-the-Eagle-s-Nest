#include "eaglenest.h"

static u8 g_block_map[MAP_SIZE] = {0};

void map_init(void) {
    memset(g_map_buffer, 0, sizeof(g_map_buffer));
    memset(g_block_map, 0, sizeof(g_block_map));
}

// Expand a level map (block indices) into the 96x168 tile buffer,
// mirroring the original MAKEMP routine in eagle.asm.
void map_load_level(u8 level) {
    if (level >= 8) return;

    memcpy(g_block_map, g_maps[level].tiles, MAP_SIZE);
    const u8* blocks = g_block_map;

    for (int my = 0; my < MAP_TILES_H; my++) {
        for (int mx = 0; mx < MAP_TILES_W; mx++) {
            u8 block_idx = blocks[my * MAP_TILES_W + mx];
            if (block_idx == 0xFF) block_idx = 0;  // enemy marker -> empty block
            if (block_idx >= 128) block_idx = 0;

            const u8* blk = &g_block_data[block_idx * 16];
            for (int r = 0; r < BLOCK_TILES_H; r++) {
                for (int c = 0; c < BLOCK_TILES_W; c++) {
                    int bx = mx * BLOCK_TILES_W + c;
                    int by = my * BLOCK_TILES_H + r;
                    g_map_buffer[by * MAP_BUFFER_W + bx] = blk[r * BLOCK_TILES_W + c];
                }
            }
        }
    }

    map_erase_detonators(level);
    objects_place_level(level);
}

u8 map_get_tile(int tile_x, int tile_y) {
    if (tile_x < 0 || tile_x >= MAP_BUFFER_W || tile_y < 0 || tile_y >= MAP_BUFFER_H) {
        return 0;
    }
    return g_map_buffer[tile_y * MAP_BUFFER_W + tile_x];
}

void map_set_tile(int tile_x, int tile_y, u8 tile) {
    if (tile_x >= 0 && tile_x < MAP_BUFFER_W && tile_y >= 0 && tile_y < MAP_BUFFER_H) {
        g_map_buffer[tile_y * MAP_BUFFER_W + tile_x] = tile;
    }
}

// Tiles the player can occupy: floor, objects (collectible), jewelry and lift.
// Doors are reported solid here; they are opened by pushing into them.
bool map_tile_walkable(u8 v) {
    if (v == 0) return true;
    if (v == 0x2A) return true;
    if (v == 0x60) return true;
    if (v >= 0x62 && v <= 0x65) return true;
    if (v == 0x68) return true;
    if (v >= 0x69 && v <= 0x6F) return true;
    if (v == 0x7C || v == 0x7D) return true;
    return false;
}

bool map_is_solid(int tile_x, int tile_y) {
    return !map_tile_walkable(map_get_tile(tile_x, tile_y));
}

// Enemies additionally treat ammo crates (0x60) as obstacles so they path
// around ammo stores instead of walking over them (deviation from the original,
// where every object tile below 0x80 is ignored by enemy collision).
bool map_is_enemy_solid(int tile_x, int tile_y) {
    if (map_is_solid(tile_x, tile_y)) return true;
    return map_get_tile(tile_x, tile_y) == 0x60;
}

// Bresenham walk between two world-pixel points. Returns false when any solid
// tile (wall or closed door) lies between them. The original stamps sprites
// into the map buffer and only registers contact when the enemy's next tile
// step lands on the player's stamp (MOVNME/CNMEBL), so a door or wall always
// separates the two and no hit is recorded. Contact damage uses this to keep
// its few-pixel reach from leaking through a one-tile-thick barrier.
bool map_line_of_sight(int x0, int y0, int x1, int y1) {
    int tx0 = x0 / TILE_W, ty0 = y0 / TILE_H;
    int tx1 = x1 / TILE_W, ty1 = y1 / TILE_H;
    int dx = abs(tx1 - tx0);
    int dy = abs(ty1 - ty0);
    int sx = (tx0 < tx1) ? 1 : -1;
    int sy = (ty0 < ty1) ? 1 : -1;
    int err = dx - dy;
    int x = tx0, y = ty0;

    for (;;) {
        if (map_is_solid(x, y)) return false;
        if (x == tx1 && y == ty1) return true;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
    }
}

bool map_is_door(int tile_x, int tile_y) {
    u8 tile = map_get_tile(tile_x, tile_y);
    return tile == 0xFF;
}

// Wooden doors (0x74 horizontal, 0x75 vertical) are broken by gunfire. Mirroring
// the original SHTHDR/SHTVDR, the whole contiguous run of door tiles is cleared.
bool map_break_wood_door(int tile_x, int tile_y) {
    u8 v = map_get_tile(tile_x, tile_y);
    if (v == 0x74) {
        int x0 = tile_x;
        while (x0 - 1 >= 0 && map_get_tile(x0 - 1, tile_y) == 0x74) x0--;
        int x1 = tile_x;
        while (x1 + 1 < MAP_BUFFER_W && map_get_tile(x1 + 1, tile_y) == 0x74) x1++;
        for (int x = x0; x <= x1; x++) map_set_tile(x, tile_y, 0);
        return true;
    }
    if (v == 0x75) {
        int y0 = tile_y;
        while (y0 - 1 >= 0 && map_get_tile(tile_x, y0 - 1) == 0x75) y0--;
        int y1 = tile_y;
        while (y1 + 1 < MAP_BUFFER_H && map_get_tile(tile_x, y1 + 1) == 0x75) y1++;
        for (int y = y0; y <= y1; y++) map_set_tile(tile_x, y, 0);
        return true;
    }
    return false;
}

// "Drunken officers" are static 2-wide officer figures painted into the map as
// tiles 0x46..0x57 (the standing ones top out at 0x4A/0x4B, the ones seated at a
// table at 0x50/0x51). Shooting any tile of a figure mirrors the original SHTCMD:
// walk up the column to the figure's top-left and replace the 2x4 block with a
// corpse pattern (CMDAT0/CMDAT1).
static const u8 CMDAT0[8] = {0x1C, 0x1D, 0x42, 0x43, 0x1A, 0x1A, 0x1A, 0x1A};
static const u8 CMDAT1[8] = {0x1A, 0x1A, 0x1A, 0x1A, 0x44, 0x45, 0x1E, 0x1F};

bool map_shot_officer(int tx, int ty, u8 d0) {
    int a0 = ty * MAP_BUFFER_W + tx;
    const u8* cmd;

    if (d0 < 0x48) {                 // 0x46/0x47: standing feet -> top 0x4A/0x4B/0x7E
        for (;;) {
            a0 -= MAP_BUFFER_W;
            if (a0 < 0) return false;
            u8 v = g_map_buffer[a0];
            if (v == 0x4A || v == 0x7E || v == 0x4B) break;
        }
        if (g_map_buffer[a0] != 0x4A) a0 -= 1;
        cmd = CMDAT1;
    } else if (d0 < 0x4A) {          // 0x48/0x49 -> seated top 0x50/0x51
        for (;;) {
            a0 -= MAP_BUFFER_W;
            if (a0 < 0) return false;
            u8 v = g_map_buffer[a0];
            if (v == 0x50 || v == 0x51) break;
        }
        if (g_map_buffer[a0] == 0x51) a0 -= 1;
        cmd = CMDAT0;
    } else if (d0 < 0x4C) {          // 0x4A/0x4B already at the top row
        if (g_map_buffer[a0] != 0x4A) a0 -= 1;
        cmd = CMDAT1;
    } else if (d0 < 0x50) {          // 0x4C..0x4F standing body
        for (;;) {
            a0 -= MAP_BUFFER_W;
            if (a0 < 0) return false;
            u8 v = g_map_buffer[a0];
            if (v == 0x4A || v == 0x7E || v == 0x4B) break;
        }
        if (g_map_buffer[a0] != 0x4A) a0 -= 1;
        cmd = CMDAT1;
    } else if (d0 < 0x52) {          // 0x50/0x51 seated top
        if (g_map_buffer[a0] == 0x51) a0 -= 1;
        cmd = CMDAT0;
    } else {                         // 0x52..0x57 seated body
        for (;;) {
            a0 -= MAP_BUFFER_W;
            if (a0 < 0) return false;
            u8 v = g_map_buffer[a0];
            if (v == 0x50 || v == 0x51) break;
        }
        if (g_map_buffer[a0] == 0x51) a0 -= 1;
        cmd = CMDAT0;
    }

    for (int r = 0; r < 4; r++) {
        int base = a0 + r * MAP_BUFFER_W;
        if (base < 0 || base + 1 >= MAP_BUFFER_SIZE) break;
        g_map_buffer[base] = cmd[r * 2];
        g_map_buffer[base + 1] = cmd[r * 2 + 1];
    }
    return true;
}

// Enemy markers (0xFF) live in the raw block map, not the tile buffer.
bool map_is_enemy_spawn(int tile_x, int tile_y) {
    if (tile_x < 0 || tile_x >= MAP_TILES_W || tile_y < 0 || tile_y >= MAP_TILES_H) {
        return false;
    }
    return g_block_map[tile_y * MAP_TILES_W + tile_x] == 0xFF;
}

void map_clear_tile(int tile_x, int tile_y) {
    map_set_tile(tile_x, tile_y, 0);
}

int map_world_to_tile_x(int world_x) {
    return world_x / TILE_W;
}

int map_world_to_tile_y(int world_y) {
    return world_y / TILE_H;
}

// Detonators, mirroring DETTBL/DETTBL2 in eagle.asm 2157-2166. Each entry is an
// (x, y) tile pair for one floor; the wall tile (0x5E) spans that tile and the
// one below. Table [castle 0=DETTBL, 1=DETTBL2][floor 0..3].
static const u8 k_dettbl[2][4][2] = {
    { {0x35, 0x62}, {0x05, 0x0A}, {0x15, 0x36}, {0x41, 0x8A} },  // castle 1
    { {0x59, 0x52}, {0x05, 0x2E}, {0x09, 0x36}, {0x55, 0x3A} },  // castle 2
};
static u8 g_det_destroyed[2][4];

void map_detonators_init(void) {
    memset(g_det_destroyed, 0, sizeof(g_det_destroyed));
}

// ERADET: on load, a detonator wall stays only while it is part of a live
// "blow up the castle" mission (DIFLVL == 3) and has not been destroyed yet;
// otherwise it is removed from the floor.
void map_erase_detonators(u8 level) {
    int c = (level >= 4) ? 1 : 0;
    int f = level & 3;
    bool keep = (g_game.dif_level == 3) && !g_det_destroyed[c][f];
    if (keep) return;
    int x = k_dettbl[c][f][0];
    int y = k_dettbl[c][f][1];
    map_set_tile(x, y, 0);
    map_set_tile(x, y + 1, 0);
}

// SHTDET: a bullet hitting the detonator wall clears the 2-tall pair and marks
// this floor's charge as set. The wall is 0x5F on top with 0x5E below; the
// original clears (a0) and (a0+96) from whichever half is struck.
bool map_detonator_hit(int tile_x, int tile_y) {
    if (g_game.dif_level != 3) return false;
    u8 v = map_get_tile(tile_x, tile_y);
    int top;
    if (v == 0x5F)      top = tile_y;
    else if (v == 0x5E) top = tile_y - 1;
    else return false;

    int c = (g_player.level >= 4) ? 1 : 0;
    int f = g_player.level & 3;
    g_det_destroyed[c][f] = 1;
    map_set_tile(tile_x, top, 0);
    map_set_tile(tile_x, top + 1, 0);
    return true;
}

// COMPM3: all four charges of the current castle have been set.
bool map_detonators_complete(u8 level) {
    int c = (level >= 4) ? 1 : 0;
    for (int f = 0; f < 4; f++) {
        if (!g_det_destroyed[c][f]) return false;
    }
    return true;
}

void map_get_tile_rect(int tile_x, int tile_y, SDL_Rect* rect) {
    rect->x = tile_x * TILE_W - g_scroll_x;
    rect->y = tile_y * TILE_H - g_scroll_y;
    rect->w = TILE_W;
    rect->h = TILE_H;
}