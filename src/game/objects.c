#include "eaglenest.h"

int g_msg_timer = 0;
int g_msg_found = 0;
char g_msg_text[24] = {0};

static u8 g_obj_killed[2][128];
static u8 g_lift_announced = 0;

static const char* const g_obj_names[10] = {
    "AN AMMO BOX", "A PAINTING", "A VASE", "A BUG", "COLD FOOD",
    "DYNAMITE", "FIRST AID", "A PENDANT", "AN ELEVATOR PASS", "A DOOR KEY"
};

void objects_init(void) {
    memset(g_obj_killed, 0, sizeof(g_obj_killed));
    g_msg_timer = 0;
    g_msg_text[0] = 0;
}

void objects_show_message(const char* text) {
    if (!g_game.opt_messages) return;  // original MSFLG gate
    snprintf(g_msg_text, sizeof(g_msg_text), "%s", text);
    g_msg_timer = 90;
    g_msg_found = 0;
}

static void objects_show_found(const char* name) {
    if (!g_game.opt_messages) return;  // original MSFLG gate
    snprintf(g_msg_text, sizeof(g_msg_text), "%s", name);
    g_msg_timer = 90;
    g_msg_found = 1;
}

static bool castle2(u8 level) { return level >= 4; }

static bool is_vertical_door(u8 v)   { return v >= 0x58 && v <= 0x5A; }
static bool is_horizontal_door(u8 v) { return v >= 0x5B && v <= 0x5D; }

static void paint_object(int x, int y, int type) {
    if (type < 0 || type > 8) return;
    const u8* p = g_obchtb[type];
    if (p[0]) map_set_tile(x,     y,     p[0]);
    if (p[1]) map_set_tile(x + 1, y,     p[1]);
    if (p[2]) map_set_tile(x,     y + 1, p[2]);
    if (p[3]) map_set_tile(x + 1, y + 1, p[3]);
}

void objects_place_level(u8 level) {
    const u8 (*tbl)[7];
    int n;
    if (castle2(level)) { tbl = g_objtbl1; n = g_objtbl1_count; }
    else                { tbl = g_objtbl0; n = g_objtbl0_count; }

    int sel = castle2(level) ? 1 : 0;
    u8 lvl = level & 3;

    for (int i = 0; i < n; i++) {
        g_obj_killed[sel][i] = 0;
        if ((tbl[i][6] & 3) == lvl) {
            paint_object(tbl[i][4], tbl[i][5], tbl[i][3] & 0x0F);
        }
    }
    g_lift_announced = 0;
}

static int find_object(int tx, int ty, u8 level) {
    const u8 (*tbl)[7];
    int n, sel;
    if (castle2(level)) { tbl = g_objtbl1; n = g_objtbl1_count; sel = 1; }
    else                { tbl = g_objtbl0; n = g_objtbl0_count; sel = 0; }
    u8 lvl = level & 3;
    for (int i = 0; i < n; i++) {
        if (g_obj_killed[sel][i]) continue;
        if ((tbl[i][6] & 3) != lvl) continue;
        int ox = tbl[i][4], oy = tbl[i][5];
        if (tx >= ox && tx <= ox + 1 && ty >= oy && ty <= oy + 1) return i;
    }
    return -1;
}

static void clear_2x2(int x, int y) {
    map_set_tile(x,     y,     0);
    map_set_tile(x + 1, y,     0);
    map_set_tile(x,     y + 1, 0);
    map_set_tile(x + 1, y + 1, 0);
}

// A bullet hit a searchable object tile: open the container and show/collect
// its contents, exactly as if the player had walked into it.
static void found_object(int tx, int ty, u8 v);

void objects_open_at(int tx, int ty) {
    u8 v = map_get_tile(tx, ty);
    if (v >= 0x60 && v <= 0x6F) found_object(tx, ty, v);
}

static void found_object(int tx, int ty, u8 v) {
    if (v == 0x60) {
        if (g_player.bullets >= 99) return;
        g_player.bullets += 15;
        if (g_player.bullets > 99) g_player.bullets = 99;
        map_set_tile(tx, ty, 0);
        audio_play_pickup();
        return;
    }
    if (v == 0x68) {
        if (g_player.keys < 99) g_player.keys++;
        map_set_tile(tx, ty, 0);
        objects_show_found(g_obj_names[9]);
        audio_play_pickup();
        return;
    }
    if (v == 0x6B) {
        g_player.has_pass = 1;
        map_set_tile(tx, ty, 0);
        objects_show_found(g_obj_names[8]);
        audio_play_pickup();
        return;
    }

    int idx = find_object(tx, ty, g_player.level);
    if (idx < 0) { map_set_tile(tx, ty, 0); return; }

    const u8 (*tbl)[7] = castle2(g_player.level) ? g_objtbl1 : g_objtbl0;
    int sel = castle2(g_player.level) ? 1 : 0;
    int type = tbl[idx][3] & 0x0F;
    g_obj_killed[sel][idx] = 1;
    clear_2x2(tbl[idx][4], tbl[idx][5]);

    switch (type) {
        case 1: g_player.score += 250; break;
        case 2: g_player.score += 150; break;
        case 3: g_player.score += 150; break;
        case 4: g_player.hits = (g_player.hits >= 16) ? (u8)(g_player.hits - 16) : 0; break;
        case 6: g_player.hits = 0; break;
        case 7: g_player.score += 400; break;
        default: break;
    }
    if (type >= 0 && type <= 9) objects_show_found(g_obj_names[type]);
    audio_play_pickup();
}

static void objects_notify_locked(void) {
    if (g_msg_timer == 0) {
        objects_show_message("IT'S LOCKED");
        audio_play_locked();
    }
}

// Original LIFT: with the elevator pass (LVLPSS) the floor-select menu opens;
// otherwise a "YOU NEED" notice. The state transition is handled by the game.
static void touch_lift(void) {
    if (g_player.has_pass) {
        g_game.lift_pending = 1;
    } else if (!g_lift_announced) {
        g_lift_announced = 1;
        objects_show_message("YOU NEED A PASS");
    }
}

void objects_player_touch(void) {
    int tx0 = g_player.x / TILE_W;
    int ty0 = g_player.y / TILE_H;
    for (int r = 0; r < SPRITE_ROWS; r++) {
        for (int c = 0; c < SPRITE_COLS; c++) {
            int tx = tx0 + c, ty = ty0 + r;
            u8 v = map_get_tile(tx, ty);
            if (v == 0) continue;
            if (v == 0x2A) { touch_lift(); continue; }
            if (v == 0x7C || v == 0x7D) {
                int top = (v == 0x7D) ? ty - 1 : ty;
                map_set_tile(tx, top, 0x7A);
                map_set_tile(tx, top + 1, 0x7B);
                g_player.score += 4;
                audio_play_pickup();
                continue;
            }
            if (v >= 0x60 && v <= 0x6F) found_object(tx, ty, v);
        }
    }
}

bool objects_try_open_door_col(int col, int y, int h) {
    for (int r = y / TILE_H; r <= (y + h - 1) / TILE_H; r++) {
        u8 v = map_get_tile(col, r);
        if (is_vertical_door(v) || is_horizontal_door(v)) {
            if (!g_cheats.alldoorsopen && g_player.keys == 0) {
                objects_notify_locked();
                return false;
            }
            if (is_vertical_door(v)) {
                int top = r - (r & 3);
                for (int i = 0; i < 4; i++) map_set_tile(col, top + i, 0);
            } else {
                int left = col - (col & 3);
                for (int i = 0; i < 4; i++) map_set_tile(left + i, r, 0);
            }
            if (!g_cheats.alldoorsopen) g_player.keys--;
            audio_play_clank();
            return true;
        }
    }
    return false;
}

bool objects_try_open_door_row(int row, int x, int w) {
    for (int c = x / TILE_W; c <= (x + w - 1) / TILE_W; c++) {
        u8 v = map_get_tile(c, row);
        if (is_vertical_door(v) || is_horizontal_door(v)) {
            if (!g_cheats.alldoorsopen && g_player.keys == 0) {
                objects_notify_locked();
                return false;
            }
            if (is_vertical_door(v)) {
                int top = row - (row & 3);
                for (int i = 0; i < 4; i++) map_set_tile(c, top + i, 0);
            } else {
                int left = c - (c & 3);
                for (int i = 0; i < 4; i++) map_set_tile(left + i, row, 0);
            }
            if (!g_cheats.alldoorsopen) g_player.keys--;
            audio_play_clank();
            return true;
        }
    }
    return false;
}
