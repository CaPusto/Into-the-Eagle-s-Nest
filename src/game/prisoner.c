#include "eaglenest.h"

// BLLCHR from eagle.asm: the prisoner sprite as 8 frame groups of 6 block
// chars (2x3 tiles). Frame index = direction_code + anim, where the direction
// codes are down=0, up=2, right=4, left=6 (matching the player's layout).
static const u8 k_prisoner_frames[8][6] = {
    {0x88, 0x89, 0x8A, 0x8B, 0xE4, 0xE5},  // down  f0
    {0x88, 0x89, 0x8A, 0x8B, 0xE6, 0xE7},  // down  f1
    {0xE0, 0xE1, 0x82, 0x83, 0x84, 0x85},  // up    f0
    {0xE2, 0xE3, 0x82, 0x83, 0x84, 0x85},  // up    f1
    {0xE8, 0x9A, 0xE9, 0x9C, 0xEA, 0x9E},  // right f0
    {0xEB, 0x9A, 0xEC, 0x9C, 0xED, 0x9E},  // right f1
    {0x90, 0xEE, 0x92, 0xEF, 0x94, 0xF0},  // left  f0
    {0x90, 0xF1, 0x92, 0xF2, 0x94, 0xF3},  // left  f1
};

Prisoner g_prisoner;

// INTBIL from eagle.asm: the hostage's floor comes from DIFTBL, indexed by the
// mission difficulty (DIFLVL). 0xFF means the mission has no hostage at all
// (mission ONE blows up the castle).
static const u8 k_diftbl[4] = {0, 2, 3, 0xFF};

// On the hostage floor the original picks one of four candidate tiles (BILTBL
// for castle 2, BILTBL2 for castle 1) at random, re-rolling while the tile
// would sit inside the opening view. Each entry is an (x, y) tile pair; the
// CELLX/CELLY bytes the original stores after it are never read, so they are
// dropped here.
static const u8 k_biltbl[4][4][2] = {
    { {8, 98}, {10, 22}, {32, 8}, {88, 60} },       // level 0 (BILTB0)
    { {8, 98}, {10, 22}, {32, 8}, {88, 60} },       // level 1 (BILTB0)
    { {85, 159}, {90, 107}, {89, 40}, {48, 79} },   // level 2 (BILTB2)
    { {6, 77}, {61, 160}, {32, 4}, {35, 131} },     // level 3 (BILTB3)
};
static const u8 k_biltbl2[4][4][2] = {
    { {8, 12}, {61, 70}, {60, 116}, {60, 116} },    // level 0 (BILTB00)
    { {8, 12}, {61, 70}, {60, 116}, {60, 116} },    // level 1 (BILTB00)
    { {36, 16}, {52, 156}, {77, 20}, {77, 20} },    // level 2 (BILTB20)
    { {48, 156}, {69, 132}, {34, 123}, {41, 64} },  // level 3 (BILTB30)
};

static bool castle2(u8 level) { return level >= 4; }

static bool prisoner_blocked_at(int px, int py) {
    int tx0 = px / TILE_W;
    int ty0 = py / TILE_H;
    for (int r = 0; r < SPRITE_ROWS; r++) {
        for (int c = 0; c < SPRITE_COLS; c++) {
            if (map_is_solid(tx0 + c, ty0 + r)) return true;
        }
    }
    return false;
}

void prisoner_init(void) {
    memset(&g_prisoner, 0, sizeof(g_prisoner));
}

// INTBIL (eagle.asm 4431): place the hostage on the floor named by DIFTBL for
// the current mission, at one of the four candidate tiles for that floor and
// castle. The candidate is chosen at random and re-rolled while it falls inside
// the opening view (SCRX/SCRY), so the hostage is never in sight at the start.
void prisoner_place_level(u8 level) {
    memset(&g_prisoner, 0, sizeof(g_prisoner));

    // INTDIF (eagle.asm 4725): if the ally rode the lift (BILLLF), he reappears
    // at the landing on the new floor and keeps following the player.
    if (g_game.bill_lift) {
        g_game.bill_lift = 0;
        g_prisoner.present = 1;
        g_prisoner.state = 1;
        g_prisoner.x = 8 * TILE_W;
        g_prisoner.y = 151 * TILE_H;
        g_prisoner.spawn_x = g_prisoner.x;
        g_prisoner.spawn_y = g_prisoner.y;
        return;
    }

    u8 idx = k_diftbl[g_game.dif_level & 3];
    if (idx == 0xFF) return;         // mission ONE: blow up the castle
    if ((level & 3) != idx) return;  // the hostage lives only on this floor

    const u8 (*row)[2] = castle2(level) ? k_biltbl[idx] : k_biltbl2[idx];

    int scrx = g_scroll_x / TILE_W;
    int scry = g_scroll_y / TILE_H;
    int pick = 0;
    for (int attempt = 0; attempt < 32; attempt++) {
        pick = random_byte() & 3;
        int x = row[pick][0];
        int y = row[pick][1];
        bool on_screen = x >= scrx && x < scrx + VIEW_W &&
                         y >= scry && y < scry + VIEW_H;
        if (!on_screen) break;
    }

    g_prisoner.present = 1;
    g_prisoner.state = 0;  // chained until he scrolls into view
    g_prisoner.x = (int)row[pick][0] * TILE_W;
    g_prisoner.y = (int)row[pick][1] * TILE_H;
    g_prisoner.spawn_x = g_prisoner.x;
    g_prisoner.spawn_y = g_prisoner.y;
    g_prisoner.frame = 0;
}

static bool prisoner_visible(void) {
    int sx = g_prisoner.x - g_scroll_x;
    int sy = g_prisoner.y - g_scroll_y;
    return sx + SPRITE_W > 0 && sx < VIEW_W * TILE_W &&
           sy + SPRITE_H > 0 && sy < VIEW_H * TILE_H;
}

void prisoner_update(void) {
    if (!g_prisoner.present || g_prisoner.state == 2) return;

    bool vis = prisoner_visible();

    // Chained: the ally breaks free and starts following once he is on screen.
    if (g_prisoner.state == 0) {
        if (vis) g_prisoner.state = 1;
        return;
    }

    if (vis) {
        g_prisoner.lost = 0;
    } else if (++g_prisoner.lost > 240) {
        // Wandered too far off-screen: back to the cell, resume chained.
        g_prisoner.x = g_prisoner.spawn_x;
        g_prisoner.y = g_prisoner.spawn_y;
        g_prisoner.state = 0;
        g_prisoner.stun = 0;
        g_prisoner.blocked = 0;
        g_prisoner.lost = 0;
        return;
    }

    if (g_prisoner.stun > 0) { g_prisoner.stun--; return; }

    // Half the player's speed: only step every other update (original BILLXX).
    g_prisoner.step_delay ^= 1;
    if (g_prisoner.step_delay) return;

    // Follow the player, aligning X before Y (original MVBLL3/MVBLL5).
    int dx = 0, dy = 0;
    if (g_prisoner.x != g_player.x) {
        dx = (g_prisoner.x < g_player.x) ? 1 : -1;
    } else if (g_prisoner.y != g_player.y) {
        dy = (g_prisoner.y < g_player.y) ? 1 : -1;
    } else {
        return; // caught up with the player
    }

    if (dx > 0)      g_prisoner.dir = 2; // right
    else if (dx < 0) g_prisoner.dir = 3; // left
    else if (dy > 0) g_prisoner.dir = 0; // down
    else             g_prisoner.dir = 1; // up

    bool moved = false;
    if (dx != 0) {
        if (!prisoner_blocked_at(g_prisoner.x + dx, g_prisoner.y)) {
            g_prisoner.x += dx;
            moved = true;
        } else if (g_prisoner.y != g_player.y) {
            int ty = (g_prisoner.y < g_player.y) ? 1 : -1;
            if (!prisoner_blocked_at(g_prisoner.x, g_prisoner.y + ty)) {
                g_prisoner.y += ty;
                moved = true;
            }
        }
    } else if (dy != 0) {
        if (!prisoner_blocked_at(g_prisoner.x, g_prisoner.y + dy)) {
            g_prisoner.y += dy;
            moved = true;
        } else if (g_prisoner.x != g_player.x) {
            int tx = (g_prisoner.x < g_player.x) ? 1 : -1;
            if (!prisoner_blocked_at(g_prisoner.x + tx, g_prisoner.y)) {
                g_prisoner.x += tx;
                moved = true;
            }
        }
    }

    if (moved) {
        g_prisoner.anim ^= 1;
        g_prisoner.blocked = 0;
    } else if (++g_prisoner.blocked > 40) {
        // Stuck against geometry: freeze until a warning shot wakes him.
        g_prisoner.stun = 120;
        g_prisoner.blocked = 0;
    }

    g_prisoner.frame = (u8)(g_prisoner.dir * 2 + g_prisoner.anim);
}

// A bullet passing the prisoner: a direct hit kills him, a nearby warning shot
// clears his stun so he starts moving again. Returns true if the bullet is
// consumed by a direct hit.
bool prisoner_bullet(int bx, int by) {
    if (!g_prisoner.present || g_prisoner.state == 2) return false;

    if (bx >= g_prisoner.x && bx < g_prisoner.x + SPRITE_W &&
        by >= g_prisoner.y && by < g_prisoner.y + SPRITE_H) {
        g_prisoner.state = 2;
        entities_spawn_death(g_prisoner.x, g_prisoner.y);
        audio_play_bang();
        objects_show_message("THE PRISONER IS DEAD");
        return true;
    }

    int cx = g_prisoner.x + SPRITE_W / 2;
    int cy = g_prisoner.y + SPRITE_H / 2;
    int dx = bx - cx, dy = by - cy;
    if (dx * dx + dy * dy <= 48 * 48) {
        g_prisoner.stun = 0;
        g_prisoner.blocked = 0;
    }
    return false;
}

// True when the ally is alive, following, and close enough to the player to be
// considered rescued (original COMPIT: BILLAC && LEVEL==BILLLV && !BILLOS).
bool prisoner_rescued(void) {
    if (!g_prisoner.present || g_prisoner.state != 1) return false;
    int dx = g_prisoner.x - g_player.x;
    int dy = g_prisoner.y - g_player.y;
    return dx * dx + dy * dy <= 64 * 64;
}

void prisoner_draw(void) {
    if (!g_prisoner.present || g_prisoner.state == 2) return;

    int sx = g_prisoner.x - g_scroll_x;
    int sy = g_prisoner.y - g_scroll_y;
    const u8* group = k_prisoner_frames[g_prisoner.frame & 7];
    for (int r = 0; r < SPRITE_ROWS; r++) {
        for (int c = 0; c < SPRITE_COLS; c++) {
            const Tile* tile = tiles_get(group[r * SPRITE_COLS + c]);
            if (tile) renderer_draw_tile(sx + c * TILE_W, sy + r * TILE_H, tile, 0);
        }
    }
}
