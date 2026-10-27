#include "eaglenest.h"
#include <stdlib.h>

// Sprite charset tiles for enemies (NMECHR), 2x3 tiles = 32x24 px.
// Indexed by sprite = direction | frame, where direction is 0=up, 2=down,
// 4=right, 6=left (see MOVNME in eagle.asm) and frame is bit 0.
static const u8 g_enemy_frames[8][6] = {
    {0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3},  // up    f0
    {0xC4, 0xC5, 0xC0, 0xC1, 0xC2, 0xC3},  // up    f1
    {0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB},  // down  f0
    {0xC6, 0xC7, 0xC8, 0xC9, 0xCC, 0xCD},  // down  f1
    {0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3},  // right f0
    {0xCE, 0xD4, 0xD0, 0xD5, 0xD2, 0xD6},  // right f1
    {0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC},  // left  f0
    {0xDD, 0xD8, 0xDE, 0xDA, 0xDF, 0xDC}   // left  f1
};

static bool rects_overlap(int ax, int ay, int aw, int ah,
                          int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

void enemies_init(void) {
    memset(g_enemies, 0, sizeof(g_enemies));
}

void enemies_spawn_from_map(void) {
    // Reset first, as the original MAKEMP does (clr.w NMEMAX + sentinel): taking
    // the lift rebuilds the floor, so the outgoing floor's guards must be
    // dropped. Otherwise they linger at their old coordinates, which land on
    // walls/objects of the new floor where they can neither move nor be shot.
    enemies_init();

    for (int ty = 0; ty < MAP_TILES_H; ty++) {
        for (int tx = 0; tx < MAP_TILES_W; tx++) {
            if (map_is_enemy_spawn(tx, ty)) {
                // Centre the 2x3 sprite inside the 4x4 block in whole tiles so
                // the enemy stays grid-aligned (it only ever steps whole tiles).
                // Centring in pixels leaves a half-tile Y offset, which makes
                // the sprite overhang a neighbouring tile (e.g. poke into a
                // closed door).
                int ex = tx * BLOCK_PIXELS_W +
                         ((BLOCK_TILES_W - SPRITE_COLS) / 2) * TILE_W;
                int ey = ty * BLOCK_PIXELS_H +
                         ((BLOCK_TILES_H - SPRITE_ROWS) / 2) * TILE_H;
                entities_spawn_enemy(ex, ey, random_byte() & 3);
            }
        }
    }
}

// Enemy occupies SPRITE_COLS x SPRITE_ROWS map tiles. Return true if any of
// those tiles is solid (wall), i.e. the sprite cannot be placed at (px,py).
static bool enemy_blocked(int px, int py) {
    int tx = px / TILE_W;
    int ty = py / TILE_H;
    for (int r = 0; r < SPRITE_ROWS; r++) {
        for (int c = 0; c < SPRITE_COLS; c++) {
            if (map_is_enemy_solid(tx + c, ty + r)) return true;
        }
    }
    return false;
}

bool enemies_occupy_tile(int tx, int ty) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &g_enemies[i];
        if ((e->flags & 0x80) == 0) continue;
        int c0 = e->x / TILE_W, c1 = (e->x + SPRITE_W - 1) / TILE_W;
        int r0 = e->y / TILE_H, r1 = (e->y + SPRITE_H - 1) / TILE_H;
        if (tx >= c0 && tx <= c1 && ty >= r0 && ty <= r1) return true;
    }
    return false;
}

// The original stamps sprites into the map buffer, so an enemy may never step
// onto tiles taken by the player or by another enemy (it then just waits).
static bool enemy_step_blocked(int px, int py, int self) {
    if (enemy_blocked(px, py)) return true;
    if (rects_overlap(px, py, SPRITE_W, SPRITE_H,
                      g_player.x, g_player.y, SPRITE_W, SPRITE_H)) return true;
    for (int j = 0; j < MAX_ENEMIES; j++) {
        if (j == self) continue;
        Enemy* o = &g_enemies[j];
        if ((o->flags & 0x80) == 0) continue;
        if (rects_overlap(px, py, SPRITE_W, SPRITE_H,
                          o->x, o->y, SPRITE_W, SPRITE_H)) return true;
    }
    return false;
}

// Only enemies near the visible window are updated, so distant enemies stay
// put instead of swarming across the whole map.
static bool enemy_in_range(const Enemy* e) {
    int margin_x = 8 * TILE_W;
    int margin_y = 8 * TILE_H;
    int sx = e->x - g_scroll_x;
    int sy = e->y - g_scroll_y;
    return sx > -margin_x - SPRITE_W && sx < VIEW_W * TILE_W + margin_x &&
           sy > -margin_y - SPRITE_H && sy < VIEW_H * TILE_H + margin_y;
}

void enemies_update(void) {
    // Original enemies step one tile per two frames in tile units. The port's
    // player moves smoothly at 2 px/frame, so enemies step one tile every
    // ENEMY_STEP_FRAMES frames to stay a bit slower than the player.
    const u32 ENEMY_STEP_FRAMES = 8;
    bool step = (g_game.game_tick % ENEMY_STEP_FRAMES) == 0;
    bool anim = ((g_game.game_tick / ENEMY_STEP_FRAMES) & 1) != 0;

    // Track the nearest enemy that actually moved, so a single footstep is
    // emitted (loudest/closest) instead of one per enemy trampling the channel.
    int step_enemy = -1;
    long step_dist2 = 0;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &g_enemies[i];
        if ((e->flags & 0x80) == 0) continue;
        if (!enemy_in_range(e)) continue;

        e->frame = anim ? 1 : 0;
        if (!step) continue;

        int ptx = (g_player.x + SPRITE_W / 2) / TILE_W;
        int pty = (g_player.y + SPRITE_H / 2) / TILE_H;
        int etx = (e->x + SPRITE_W / 2) / TILE_W;
        int ety = (e->y + SPRITE_H / 2) / TILE_H;

        int ddx = (ptx > etx) ? 1 : (ptx < etx) ? -1 : 0;
        int ddy = (pty > ety) ? 1 : (pty < ety) ? -1 : 0;

        if (ddx == 0 && ddy == 0) continue;

        // Try the preferred axis first, then the other. If both are blocked the
        // enemy simply waits (no jitter), so it never looks like random motion.
        int best_dx = 0, best_dy = 0;
        if (e->flags & 0x10) {          // horizontal first
            if (ddx != 0 && !enemy_step_blocked(e->x + ddx * TILE_W, e->y, i)) {
                best_dx = ddx;
            } else if (ddy != 0 && !enemy_step_blocked(e->x, e->y + ddy * TILE_H, i)) {
                best_dy = ddy;
            } else {
                continue;               // both directions blocked this step
            }
        } else {                        // vertical first
            if (ddy != 0 && !enemy_step_blocked(e->x, e->y + ddy * TILE_H, i)) {
                best_dy = ddy;
            } else if (ddx != 0 && !enemy_step_blocked(e->x + ddx * TILE_W, e->y, i)) {
                best_dx = ddx;
            } else {
                continue;
            }
        }

        e->x += best_dx * TILE_W;
        e->y += best_dy * TILE_H;

        // Face the direction just moved (0=up, 2=down, 4=right, 6=left), keeping
        // the animation bit, mirroring MOVNME's `or.b d0,(2,a6)`.
        int base;
        if (best_dy < 0)      base = 0;
        else if (best_dy > 0) base = 2;
        else if (best_dx > 0) base = 4;
        else                  base = 6;
        e->state = (u8)(base | (e->frame & 1));

        long dx = (long)(e->x - g_player.x);
        long dy = (long)(e->y - g_player.y);
        long d2 = dx * dx + dy * dy;
        if (step_enemy < 0 || d2 < step_dist2) {
            step_enemy = i;
            step_dist2 = d2;
        }
    }

    // Enemies are not silent: the closest mover emits a positional footstep so
    // the player can hear one approaching from off-screen or around a corner.
    if (step_enemy >= 0) {
        const Enemy* e = &g_enemies[step_enemy];
        audio_play_step_at(e->x + SPRITE_W / 2, e->y + SPRITE_H,
                           g_player.x + SPRITE_W / 2, g_player.y + SPRITE_H / 2);
    }
}

static void enemies_draw_sprite(int x, int y, const u8 group[6]) {
    for (int r = 0; r < SPRITE_ROWS; r++) {
        for (int c = 0; c < SPRITE_COLS; c++) {
            const Tile* tile = tiles_get(group[r * SPRITE_COLS + c]);
            if (tile) {
                renderer_draw_tile(x + c * TILE_W, y + r * TILE_H, tile, 0);
            }
        }
    }
}

void enemies_draw(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &g_enemies[i];
        if ((e->flags & 0x80) == 0) continue;

        const u8* group = g_enemy_frames[e->state & 7];
        enemies_draw_sprite(e->x - g_scroll_x, e->y - g_scroll_y, group);
    }
}

// Draw one enemy sprite frame (dir|frame) at absolute screen coordinates. Used
// by the attract screen's patrolling guards (TTANIM/FAKENM/FAKEN0).
void enemies_draw_sprite_at(int x, int y, u8 sprite) {
    enemies_draw_sprite(x, y, g_enemy_frames[sprite & 7]);
}

bool enemies_check_player_collision(void) {
    // Enemies stop one tile away from the player (tile-level blocking), so the
    // damage test uses rects expanded by a few pixels to trigger on contact.
    // A closed door or wall between them blocks the reach: the original only
    // lands a hit when the enemy's next tile step would overlap the player
    // (MOVNME/CNMEBL), never across a barrier.
    int pcx = g_player.x + SPRITE_W / 2;
    int pcy = g_player.y + SPRITE_H / 2;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &g_enemies[i];
        if ((e->flags & 0x80) == 0) continue;

        if (entities_check_collision(
                g_player.x - 8, g_player.y - 8, SPRITE_W + 16, SPRITE_H + 16,
                e->x - 8, e->y - 8, SPRITE_W + 16, SPRITE_H + 16)) {
            if (!map_line_of_sight(pcx, pcy,
                                   e->x + SPRITE_W / 2, e->y + SPRITE_H / 2)) {
                continue;
            }
            return true;
        }
    }
    return false;
}

bool enemies_damage_at(int x, int y) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &g_enemies[i];
        if ((e->flags & 0x80) == 0) continue;

        // The original stamps the whole 2x3 enemy sprite into the map buffer,
        // so any bullet touching the sprite kills the enemy. A few pixels of
        // margin absorb grazing shots that pass along the sprite's edge, which
        // players expect to count as a hit.
        if (entities_check_collision(
                x - 4, y - 4, 8, 8,
                e->x - 4, e->y - 4, SPRITE_W + 8, SPRITE_H + 8)) {
            // A closed door or wall between the bullet and the enemy blocks the
            // shot: the enlarged hitbox must never let a bullet reach an enemy
            // through a barrier the bullet itself cannot pass (original checks
            // the stamped tile, never across a solid tile).
            if (!map_line_of_sight(x, y,
                                   e->x + SPRITE_W / 2, e->y + SPRITE_H / 2)) {
                continue;
            }
            // DFCLTY (original KLLNME): on HARD a guard survives the first hit
            // (bit 5 in the original) and only dies to the second.
            if (g_game.opt_difficulty && !(e->flags & 0x40)) {
                e->flags |= 0x40;
                effects_spawn_sparks(x, y);
                player_add_score(50);
                return true;
            }
            e->flags |= 0x20;
            entities_spawn_death(e->x + SPRITE_W / 2, e->y + SPRITE_H / 2);
            effects_spawn_sparks(x, y);
            e->flags &= ~0x80;
            player_add_score(50);
            audio_play_bang();
            return true;
        }
    }
    return false;
}