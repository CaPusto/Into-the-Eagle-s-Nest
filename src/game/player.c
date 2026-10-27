#include "eaglenest.h"

// Sprite charset tiles (indexes into charset 0x80..), 2x3 tiles = 32x24 px per frame.
// YCHRS groups in original order: down 0/1, up 2/3, left 4/5, right 6/7, then
// the four firing poses 8..11 (YFRAME = dir + 8) whose artwork has the gun
// raised and the muzzle flash drawn in.
// Direction codes used throughout: 0=down, 1=up, 2=left, 3=right.
static const u8 g_player_frames[12][6] = {
    {0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D},  // down f0
    {0x88, 0x89, 0x8A, 0x8B, 0x8E, 0x8F},  // down f1
    {0x80, 0x81, 0x82, 0x83, 0x84, 0x85},  // up   f0
    {0x86, 0x87, 0x82, 0x83, 0x84, 0x85},  // up   f1
    {0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E},  // left f0
    {0x9F, 0x9A, 0xA0, 0x9C, 0xA1, 0x9E},  // left f1
    {0x90, 0x91, 0x92, 0x93, 0x94, 0x95},  // right f0
    {0x90, 0x96, 0x92, 0x97, 0x94, 0x98},  // right f1
    {0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD},  // fire down
    {0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7},  // fire up
    {0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9},  // fire left
    {0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3}   // fire right
};

// Original FIREFL latch: exactly one shot per fire-button press. While the
// button is held the latch stays set and the firing pose is kept; releasing it
// clears the latch so the next press fires again.
static int s_fire_latch = 0;
// While the fire button is held the firing pose (frame dir+8, muzzle flash
// baked in) is shown.
static int s_fire_pose_dir = 0;
// Last facing direction (0=down,1=up,2=left,3=right) so the pose can be
// dropped back to a valid normal frame even when no direction is held.
static int s_last_dir = 0;
// Counts moved frames so a footstep plays at a steady cadence while walking.
static int s_step_phase = 0;

static void player_draw_sprite(int x, int y, const u8 group[6]) {
    for (int r = 0; r < SPRITE_ROWS; r++) {
        for (int c = 0; c < SPRITE_COLS; c++) {
            const Tile* tile = tiles_get(group[r * SPRITE_COLS + c]);
            if (tile) {
                renderer_draw_tile(x + c * TILE_W, y + r * TILE_H, tile, 0);
            }
        }
    }
}

// Original S19/S16 (eagle.asm 982-994): XLEVEL == 0xFE (new game / next
// mission) starts the player at the entrance tile (47,161); a lift ride
// (XLEVEL = 0xFF) lands at (10,151) instead.
void player_start_level(void) {
    if (g_game.start_at_entrance) {
        g_player.x = 47 * TILE_W;
        g_player.y = 161 * TILE_H;
    } else {
        g_player.x = 10 * TILE_W;
        g_player.y = 151 * TILE_H;
    }
    g_player.old_x = g_player.x;
    g_player.old_y = g_player.y;
    g_player.frame = 0;  // down, frame 0
    g_player.old_frame = 0;
    s_last_dir = 0;
    s_fire_latch = 0;
    s_fire_pose_dir = 0;
}

void player_init(void) {
    g_player.x = 10 * TILE_W;
    g_player.y = 151 * TILE_H;
    g_player.old_x = g_player.x;
    g_player.old_y = g_player.y;
    g_player.frame = 0;  // down, frame 0
    g_player.old_frame = 0;
    g_player.bullets = 99;
    g_player.hits = 0;
    g_player.keys = 0;
    g_player.score = 0;
    g_player.level = (u8)g_cheats.start_level;
    g_player.xlevel = 0;
    g_player.bill_x = 0;
    g_player.bill_y = 0;
    g_player.bill_level = 0;
    g_player.bill_active = 0;
    g_player.bill_speed = 0;
    g_player.bill_frame = 0;
    g_player.bill_flags = 0;
    s_fire_latch = 0;
    s_fire_pose_dir = 0;
    s_last_dir = 0;
    s_step_phase = 0;
    g_player.has_pass = 0;
    g_player.hit_cooldown = 0;
    player_start_level();
}

// True if the map tile column `col` contains a solid tile anywhere across the
// vertical pixel span [y, y+h) of the sprite.
// Enemies are stamped into the buffer in the original, so enemy-occupied tiles
// block the player just like walls: the player may not step onto an enemy.
static bool player_blocked_col(int col, int y, int h) {
    for (int r = y / TILE_H; r <= (y + h - 1) / TILE_H; r++) {
        if (map_is_solid(col, r)) return true;
        if (map_get_tile(col, r) == 0x60 && g_player.bullets >= 99) return true;
        if (enemies_occupy_tile(col, r)) return true;
    }
    return false;
}

// True if the map tile row `row` contains a solid tile anywhere across the
// horizontal pixel span [x, x+w) of the sprite.
static bool player_blocked_row(int row, int x, int w) {
    for (int c = x / TILE_W; c <= (x + w - 1) / TILE_W; c++) {
        if (map_is_solid(c, row)) return true;
        if (map_get_tile(c, row) == 0x60 && g_player.bullets >= 99) return true;
        if (enemies_occupy_tile(c, row)) return true;
    }
    return false;
}

// Move horizontally by dx pixels, stopping exactly at a solid tile edge.
static void player_move_x(int dx) {
    int x = g_player.x;
    int y = g_player.y;
    if (dx > 0) {
        int cur_col = (x + SPRITE_W - 1) / TILE_W;
        int new_col = (x + SPRITE_W - 1 + dx) / TILE_W;
        if (new_col > cur_col && player_blocked_col(new_col, y, SPRITE_H)) {
            if (objects_try_open_door_col(new_col, y, SPRITE_H) &&
                !player_blocked_col(new_col, y, SPRITE_H)) {
                g_player.x = x + dx;
                return;
            }
            g_player.x = new_col * TILE_W - SPRITE_W;  // hug the wall
        } else {
            g_player.x = x + dx;
        }
    } else if (dx < 0) {
        int cur_col = x / TILE_W;
        int new_col = (x + dx) / TILE_W;
        if (new_col < cur_col && player_blocked_col(new_col, y, SPRITE_H)) {
            if (objects_try_open_door_col(new_col, y, SPRITE_H) &&
                !player_blocked_col(new_col, y, SPRITE_H)) {
                g_player.x = x + dx;
                return;
            }
            g_player.x = (new_col + 1) * TILE_W;  // hug the wall
        } else {
            g_player.x = x + dx;
        }
    }
}

// Move vertically by dy pixels, stopping exactly at a solid tile edge.
static void player_move_y(int dy) {
    int x = g_player.x;
    int y = g_player.y;
    if (dy > 0) {
        int cur_row = (y + SPRITE_H - 1) / TILE_H;
        int new_row = (y + SPRITE_H - 1 + dy) / TILE_H;
        if (new_row > cur_row && player_blocked_row(new_row, x, SPRITE_W)) {
            if (objects_try_open_door_row(new_row, x, SPRITE_W) &&
                !player_blocked_row(new_row, x, SPRITE_W)) {
                g_player.y = y + dy;
                return;
            }
            g_player.y = new_row * TILE_H - SPRITE_H;  // hug the wall
        } else {
            g_player.y = y + dy;
        }
    } else if (dy < 0) {
        int cur_row = y / TILE_H;
        int new_row = (y + dy) / TILE_H;
        if (new_row < cur_row && player_blocked_row(new_row, x, SPRITE_W)) {
            if (objects_try_open_door_row(new_row, x, SPRITE_W) &&
                !player_blocked_row(new_row, x, SPRITE_W)) {
                g_player.y = y + dy;
                return;
            }
            g_player.y = (new_row + 1) * TILE_H;  // hug the wall
        } else {
            g_player.y = y + dy;
        }
    }
}

void player_update(void) {
    if (g_player.hit_cooldown) g_player.hit_cooldown--;

    if (g_cheats.unlimitamo) g_player.bullets = 99;

    g_player.old_x = g_player.x;
    g_player.old_y = g_player.y;
    g_player.old_frame = g_player.frame;

    u8 joy = g_input.joy_val;

    int dx = 0, dy = 0;
    int dir = -1;

    if (joy & JOY_UP)   { dy = -1; dir = 1; }
    if (joy & JOY_DOWN) { dy = 1;  dir = 0; }
    if (joy & JOY_LEFT) { dx = -1; dir = 2; }
    if (joy & JOY_RIGHT){ dx = 1;  dir = 3; }

    if (dir >= 0) {
        s_last_dir = dir;
        player_move_x(dx * 2);
        player_move_y(dy * 2);
        // The original sets the facing (YFRAME) as soon as a direction is
        // pressed and only reverts the position if the move is blocked, so the
        // player can turn while pressed against a door, wall or enemy.
        u8 anim = (g_game.game_tick / 4) & 1;
        g_player.frame = dir * 2 + anim;
    } else {
        g_player.frame = s_last_dir * 2;  // keep direction, clear anim bit
    }

    // Footstep cadence: a quiet tap while the player is actually moving.
    bool moved = (g_player.x != g_player.old_x) || (g_player.y != g_player.old_y);
    if (moved) {
        if (++s_step_phase >= 10) {
            s_step_phase = 0;
            int cx = g_player.x + SPRITE_W / 2;
            int cy = g_player.y + SPRITE_H;
            audio_play_step_at(cx, cy, cx, g_player.y + SPRITE_H / 2);
        }
    } else {
        s_step_phase = 0;
    }

    if (joy & JOY_FIRE) {
        if (!s_fire_latch && g_player.bullets > 0) {
            player_fire();
            s_fire_latch = 1;
        }
    } else {
        s_fire_latch = 0;
    }

    // The original keeps the firing pose (YFRAME = dir + 8, gun raised with the
    // muzzle flash drawn in) for as long as the fire button is held, then
    // restores the walking frame on release.
    if (s_fire_latch) {
        g_player.frame = s_fire_pose_dir + 8;
    }

    objects_player_touch();

    int max_x = MAP_BUFFER_W * TILE_W - SPRITE_W;
    int max_y = MAP_BUFFER_H * TILE_H - SPRITE_H;
    if (g_player.x < 0) g_player.x = 0;
    if (g_player.y < 0) g_player.y = 0;
    if (g_player.x > max_x) g_player.x = max_x;
    if (g_player.y > max_y) g_player.y = max_y;
}

void player_fire(void) {
    if (g_player.bullets == 0) return;

    int dir = s_last_dir;
    int bx = g_player.x + SPRITE_W / 2;
    int by = g_player.y + SPRITE_H / 2;

    switch (dir) {
        case 0: by += 8; break;
        case 1: by -= 8; break;
        case 2: bx -= 8; break;
        case 3: bx += 8; break;
    }

    entities_spawn_bullet(bx, by, dir, true);
    if (!g_cheats.unlimitamo) g_player.bullets--;
    audio_play_gun();

    // The firing pose (gun raised, muzzle flash) is shown while fire is held.
    s_fire_pose_dir = dir;
}

void player_draw(void) {
    int frame = g_player.frame & 0x0F;
    if (frame >= 12) frame = 0;
    player_draw_sprite(g_player.x - g_scroll_x, g_player.y - g_scroll_y,
                       g_player_frames[frame]);
}

void player_take_damage(int amount) {
    if (g_cheats.invulnerability) return;
    g_player.hits += amount;
}

// Instant kill (e.g. detonating dynamite): the playing update turns this into
// the death sequence.
void player_kill(void) {
    if (g_cheats.invulnerability) return;
    g_player.hits = 51;
}

void player_add_score(int points) {
    g_player.score += points;
}

void player_add_keys(int count) {
    g_player.keys += count;
}