#include "eaglenest.h"
#include <stdio.h>

// Debug: dump current framebuffer to a PPM file (set EAGLE_DUMP_DIR).
static void debug_dump_ppm(const char* name) {
    const char* dir = getenv("EAGLE_DUMP_DIR");
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            u32 c = g_framebuffer.pixels[y][x];
            u8 rgb[3] = {(u8)((c >> 16) & 0xFF), (u8)((c >> 8) & 0xFF), (u8)(c & 0xFF)};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("DUMP %s\n", name);
}

typedef enum {
    STATE_TITLE = 0,
    STATE_MISSION_SELECT = 1,
    STATE_BRIEFING = 2,
    STATE_PLAYING = 3,
    STATE_PAUSED = 4,
    STATE_DEAD = 5,
    STATE_MISSION_COMPLETE = 6,
    STATE_GAME_OVER = 7,
    STATE_MISSION_FAILED = 8,
    STATE_LIFT_MENU = 9,
    STATE_OPTIONS = 10,
    STATE_EXPLOSION = 11,
    STATE_NAME_ENTRY = 12
} GameStateEnum;

// Original MSNTB: mission names shown on the brief (mission ONE..FOUR).
static const char* const kMissionNames[4] = {"ONE", "TWO", "THREE", "FOUR"};

static int g_title_timer = 0;
static int g_briefing_timer = 0;
static int g_death_timer = 0;
static int g_fail_timer = 0;
static int g_explosion_timer = 0;  // frames left of the lethal-blast flash
static int g_game_over_timer = 0;  // frames on the GAME OVER screen (10 s timeout)
static u8 g_sel_prev_joy = 0;    // rising-edge tracker for the LEVEL SELECT screen
static u8 g_title_prev_joy = 0;  // rising-edge tracker so a held FIRE (from GAME OVER) does not skip TITLE
static u8 g_brief_prev_joy = 0;  // rising-edge tracker so the confirm press isn't reused

// ---- Attract screens (title state) ---------------------------------------
// Original boot/attract order: Mindscape picture (data/ms.raw) -> title picture
// (data/scrn.raw) -> S10 loop which cycles TTLTBL pages for ~510 frames each:
// TTL0 welcome, TTL1 high scores, TTL2 valuables, TTL3 supplies. CLS always
// blits the HUD panel (right 64px column + bottom 8px strip), so every
// attract page keeps the panel.
enum {
    TP_MINDSCAPE = 0,   // data/ms.raw
    TP_TITLE     = 1,   // data/scrn.raw
    TP_WELCOME   = 2,   // TTL0
    TP_HEROES    = 3,   // TTL1 (high score table)
    TP_VALUABLES = 4,   // TTL2
    TP_SUPPLIES  = 5,   // TTL3
    TP_COUNT
};
#define TP_LOGO_FRAMES    180  // mindscape/title picture dwell (port timing)
#define TP_ATTRACT_FRAMES 600  // ~10 s per attract page (original ~510 @50fps)

static int g_title_page = TP_MINDSCAPE;
static int g_title_page_t = 0;  // frames spent on the current page

// TTANIM state for the two guards patrolling the WELCOME brick floor.
static int g_tta_pos = 4;      // TTAPOS
static int g_tta_dir = 1;      // TTADIR
static int g_tta_cnt = 8;      // TTACNT steps left before turning round
static int g_tta_delay = 1;    // TTADLY frames until the next step
static int g_tta_facing = 4;   // FAKENM direction (4=right, 6=left)
static int g_tta_frame = 0;    // walk animation bit

static void title_reset(void) {
    g_title_timer = 0;
    g_title_page = TP_MINDSCAPE;
    g_title_page_t = 0;
    g_title_prev_joy = g_input.joy_val;  // a held FIRE must not open the options menu
}

static void title_enter_welcome(void) {
    g_title_page = TP_WELCOME;
    g_title_page_t = 0;
    g_tta_pos = 4;
    g_tta_dir = 1;
    g_tta_cnt = 8;
    g_tta_delay = 1;
    g_tta_facing = 4;
    g_tta_frame = 0;
}

static void camera_update(void);
static void game_load_floor(u8 level);

void game_init(void) {
    memory_init();
    palette_init();
    tiles_init();
    map_init();
    entities_init();
    player_init();
    enemies_init();
    bullets_init();
    effects_init();
    objects_init();
    map_detonators_init();
    input_init();

    if (!tiles_load_all_assets("assets")) {
        SDL_Log("Warning: Could not load all assets, using defaults");
    }

    highscores_init();

    screen_load_planar("assets", "ms", &g_screen_ms, true);
    screen_load_planar("assets", "scrn", &g_screen_title, true);
    screen_load_planar("assets", "panel", &g_screen_panel, false);

    audio_init();
    audio_load_assets("assets");
    audio_start_music();

    g_game.game_state = STATE_TITLE;
    title_reset();
    g_game.mission_select = 0;
    g_game.dif_level = 3;  // default: MISSION ONE - blow up the castle
    g_game.mission_num = 0;
    g_game.opt_cursor = 0;
    g_game.opt_difficulty = 1;  // original DFCLTY default: HARD
    g_game.castle = 0;          // original LOADED default: castle 1
    g_game.opt_messages = 1;    // original MSFLG default: messages on

    prisoner_init();
}

bool game_in_gameplay(void) {
    return g_game.game_state == STATE_PLAYING || g_game.game_state == STATE_PAUSED ||
           g_game.game_state == STATE_LIFT_MENU;
}

void game_shutdown(void) {
    audio_shutdown();
    input_shutdown();
    platform_shutdown();
}

// ---- Attract page artwork -------------------------------------------------
// Faithful re-implementation of WPRT/WSPRNT/WSLINE/PCHRMP/TTLBK0 in cells of
// 16x8 px. Colour comes from the standard COLS palette (palette_init).

static void title_tile(int cx, int cy, u8 idx) {
    const Tile* t = tiles_get(idx);
    if (t) renderer_draw_tile(cx * TILE_W, cy * TILE_H, t, 0);
}

// WSPRNT: plain text starting at cell (cx,cy).
static void title_text(int cx, int cy, const char* s) {
    renderer_draw_string(cx * TILE_W, cy * TILE_H, s, 0);
}

// The underlined bar row used by WSLINE (CHARTBL[$29] -> glyph 41).
static void title_bars(int cx, int cy, int n) {
    for (int i = 0; i < n; i++)
        renderer_draw_char((cx + i) * TILE_W, cy * TILE_H, 41, 0);
}

// WSLINE: text plus a bar row one cell below.
static void title_wline(int cx, int cy, const char* s) {
    title_text(cx, cy, s);
    title_bars(cx, cy + 1, (int)strlen(s));
}

// TTLBK0/TTLBAK: fill a w x h cell block with tile 0 (brick).
static void title_brick(int cx, int cy, int w, int h) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            title_tile(cx + x, cy + y, 0);
}

// WPOBJ0: one 16x32 item icon (two stacked tiles).
static void title_icon(int cx, int cy, u8 top, u8 bottom) {
    title_tile(cx, cy, top);
    title_tile(cx, cy + 1, bottom);
}

// TTL0: welcome screen with a brick floor and two patrolling guards.
static void title_draw_welcome(void) {
    title_wline(1, 1, "WELCOME TO THE");
    title_wline(2, 4, "EAGLES  NEST");
    // INTRO2 is 0,'PRESS FIRE' so the leading cell stays blank.
    renderer_draw_string(2 * TILE_W, 20 * TILE_H, " ", 0);
    title_wline(3, 20, "PRESS FIRE");
    title_brick(1, 8, 14, 10);
}

static void title_draw_welcome_guards(void) {
    u8 m = (u8)(g_tta_facing ^ g_tta_frame);
    u8 o = (u8)(m ^ 2);  // FAKEN0 mirrors FAKENM's direction
    enemies_draw_sprite_at(g_tta_pos * TILE_W, 9 * TILE_H, m);
    enemies_draw_sprite_at((14 - g_tta_pos) * TILE_W, 13 * TILE_H, o);
}

// TTLANIM: one guard step, every ~12 frames (TTADLY=6 original ticks, the
// original calls TTANIM once per two frames).
static void title_ttl0_update(void) {
    if (--g_tta_delay > 0) return;
    g_tta_delay = 12;
    g_tta_pos += g_tta_dir;
    if (--g_tta_cnt <= 0) {
        g_tta_cnt = 8;
        g_tta_dir = -g_tta_dir;
        g_tta_facing ^= 2;
    }
    g_tta_frame ^= 1;
}

// TTL2: valuables screen (text right, brick strip + item icons left).
static void title_draw_valuables(void) {
    title_wline(4, 2, "VALUABLES");
    title_text(6, 7, "CLASSIC");
    title_text(6, 8, " PAINTING");
    title_text(6, 11, "PRICELESS");
    title_text(6, 12, "  VASE");
    title_text(6, 15, "GOLDEN");
    title_text(6, 16, "  PENDANT");
    title_text(6, 19, "ASSORTED");
    title_text(6, 20, "GEMSTONES");
    title_brick(2, 6, 3, 16);
    title_icon(3, 7, 0x62, 0x63);
    title_icon(3, 11, 0x64, 0x65);
    title_icon(3, 15, 0x6E, 0x6F);
    title_icon(3, 19, 0x7C, 0x7D);
}

// TTL3: supplies screen.
static void title_draw_supplies(void) {
    title_wline(4, 2, "SUPPLIES");
    title_text(6, 7, "COLD FOOD");
    title_text(6, 11, "DOOR KEY");
    title_text(6, 15, "AMMO BOX");
    title_text(6, 19, "FIRST AID");
    title_brick(2, 6, 3, 16);
    title_icon(3, 7, 0x69, 0x6A);
    title_icon(3, 11, 0x68, 0x00);
    title_icon(3, 15, 0x60, 0x00);
    title_icon(3, 19, 0x6C, 0x6D);
}

// TTL1: the high-score table ("CURRENT  HEROES"). Nine records, one every other
// row (cells 3,5,..,19); the name occupies the first eight cells and the score
// the six cells at column 10 (the port prints it as decimal instead of the
// original's three hex bytes).
static void title_draw_heroes(void) {
    title_wline(0, 0, "CURRENT  HEROES");
    for (int i = 0; i < HS_ENTRIES; i++) {
        int cy = 3 + i * 2;
        for (int c = 0; c < HS_NAME_LEN; c++)
            renderer_draw_char(c * TILE_W, cy * TILE_H, g_highscores[i].name[c], 0);
        u32 s = g_highscores[i].score > 999999u ? 999999u : g_highscores[i].score;
        char sbuf[8];
        snprintf(sbuf, sizeof(sbuf), "%06u", s);
        for (int c = 0; c < 6; c++)
            renderer_draw_char((10 + c) * TILE_W, cy * TILE_H,
                               renderer_char_to_glyph(sbuf[c]), 0);
    }
    // INTRO2 is 0,'PRESS FIRE', so the leading cell stays blank.
    renderer_draw_string(2 * TILE_W, 22 * TILE_H, " ", 0);
    title_wline(3, 22, "PRESS FIRE");
}

// Original S10: dwell on a page for its slot, then advance. FIRE goes to the
// setup menu from any page (btst #4 JOYVAL -> CTRLMNU).
static void game_update_title(void) {
    g_title_timer++;
    g_title_page_t++;

    if (g_title_page == TP_WELCOME) title_ttl0_update();

    int dwell = (g_title_page <= TP_TITLE) ? TP_LOGO_FRAMES : TP_ATTRACT_FRAMES;
    if (g_title_page_t >= dwell) {
        if (g_title_page == TP_TITLE) {
            title_enter_welcome();
        } else if (g_title_page >= TP_SUPPLIES) {
            title_enter_welcome();
        } else {
            g_title_page_t = 0;
            g_title_page++;
        }
    }

    u8 tjoy = g_input.joy_val;
    u8 tpressed = (u8)(tjoy & ~g_title_prev_joy);
    g_title_prev_joy = tjoy;
    if (tpressed & JOY_FIRE) {
        g_game.game_state = STATE_OPTIONS;
        g_game.opt_cursor = 0;
        g_game.opt_prev_joy = tjoy;  // require a fresh press on the next screen
    }
}

// Original CTRLMNU: a setup menu. Rows 0..4 are two-way toggles (DIFFICULTY,
// CASTLE, SOUND, MUSIC, MESSAGES); row 5 starts the game, row 6 quits.
// Up/down move the cursor; left/right or fire toggle the highlighted row.
enum { OPT_ROWS = 7, OPT_START = 5, OPT_QUIT = 6 };

static void game_update_options(void) {
    u8 joy = g_input.joy_val;
    u8 pressed = (u8)(joy & ~g_game.opt_prev_joy);
    g_game.opt_prev_joy = joy;

    if (pressed & JOY_UP) {
        if (g_game.opt_cursor > 0) g_game.opt_cursor--;
    }
    if (pressed & JOY_DOWN) {
        if (g_game.opt_cursor < OPT_ROWS - 1) g_game.opt_cursor++;
    }

    // Left/right pick the value of the highlighted two-way option.
    if (g_game.opt_cursor < OPT_START && ((pressed & JOY_LEFT) || (pressed & JOY_RIGHT))) {
        switch (g_game.opt_cursor) {
            case 0: g_game.opt_difficulty ^= 1; break;
            case 1: g_game.castle ^= 1; break;
            case 2: audio_toggle_sfx(); break;
            case 3: audio_toggle_music(); break;
            case 4: g_game.opt_messages ^= 1; break;
            default: break;
        }
    }

    if (pressed & JOY_FIRE) {
        if (g_game.opt_cursor == OPT_START) {
            // START: proceed to the mission (difficulty) selection.
            g_game.game_state = STATE_MISSION_SELECT;
            g_sel_prev_joy = joy;
            return;
        }
        if (g_game.opt_cursor == OPT_QUIT) {
            g_game.running = false;
            return;
        }
        switch (g_game.opt_cursor) {
            case 0: g_game.opt_difficulty ^= 1; break;
            case 1: g_game.castle ^= 1; break;
            case 2: audio_toggle_sfx(); break;
            case 3: audio_toggle_music(); break;
            case 4: g_game.opt_messages ^= 1; break;
            default: break;
        }
    }
}

// Original GETDIF ("LEVEL SELECT"): left/right choose mission 1..4, fire starts.
static void game_update_mission_select(void) {
    u8 joy = g_input.joy_val;
    u8 pressed = (u8)(joy & ~g_sel_prev_joy);  // rising edges only
    g_sel_prev_joy = joy;

    if (pressed & JOY_LEFT) {
        if (g_game.mission_select > 0) g_game.mission_select--;
    }
    if (pressed & JOY_RIGHT) {
        if (g_game.mission_select < 3) g_game.mission_select++;
    }
    if (pressed & JOY_FIRE) {
        // Original START_GAME: DIFLVL = 3 - selection. DIFLVL >= 3 (mission ONE)
        // means "blow up the castle", otherwise the mission requires a rescue.
        g_game.dif_level = (u8)(3 - g_game.mission_select);
        g_game.mission_num = 0;          // START_GAME resets MSNNUM
        g_game.start_at_entrance = 1;    // original XLEVEL = 0xFE (LEVEL 1, 47,161)
        // LOADED selects the castle (default start is GROUND). An explicit
        // --level debug value is respected as-is.
        if (g_cheats.start_level == 1)
            g_player.level = (u8)(g_game.castle * 4 + 1);
        map_detonators_init();           // START_GAME clears the four charge flags
        g_game.game_state = STATE_BRIEFING;
        g_briefing_timer = 0;
        g_brief_prev_joy = joy;  // the confirming press must not skip the brief
    }
}

static void game_update_briefing(void) {
    g_briefing_timer++;
    u8 joy = g_input.joy_val;
    u8 pressed = (u8)(joy & ~g_brief_prev_joy);
    g_brief_prev_joy = joy;
    if (g_briefing_timer > 180 || (pressed & JOY_FIRE)) {
        g_game.game_state = STATE_PLAYING;
        audio_stop_music();  // music plays only on the intro/brief screens
        game_load_floor(g_player.level);  // original S19/S16 level build
    }
}

// Original S16: (re)build the current floor and restart the player at the lift
// landing (10,151), or at the mission entrance (47,161) when start_at_entrance
// is set (brand-new game / next mission).
static void game_load_floor(u8 level) {
    g_player.has_pass = 0;  // original S24: clr LVLPSS - find a pass on each floor
    map_load_level(level);
    enemies_spawn_from_map();
    player_start_level();
    camera_update();  // snap camera to player before placing the hostage
    prisoner_place_level(level);
    g_game.frame_counter = 0;
}

static void camera_update(void) {
    int cx = g_player.x + SPRITE_W / 2 - (VIEW_W * TILE_W) / 2;
    int cy = g_player.y + SPRITE_H / 2 - (VIEW_H * TILE_H) / 2;
    int max_x = MAP_BUFFER_W * TILE_W - VIEW_W * TILE_W;
    int max_y = MAP_BUFFER_H * TILE_H - VIEW_H * TILE_H;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx > max_x) cx = max_x;
    if (cy > max_y) cy = max_y;
    renderer_set_scroll(cx, cy);
}

static void game_update_playing(void) {
    if (g_input.pause_requested) {
        g_game.paused = !g_game.paused;
        g_input.pause_requested = false;
        if (g_game.paused) {
            g_game.game_state = STATE_PAUSED;
        }
        return;
    }

    if (g_game.paused) return;

    // The player stepped onto a lift tile: open the SELECT FLOOR menu.
    if (g_game.lift_pending) {
        g_game.lift_pending = 0;
        g_game.lift_floor = g_player.level & 3;
        g_game.lift_prev_joy = g_input.joy_val;
        // LIFT (3251-3259): BILLLF is set when the ally is following on his
        // floor, so he rides the elevator down to the selected floor.
        g_game.bill_lift = (g_prisoner.present && g_prisoner.state == 1) ? 1 : 0;
        g_game.game_state = STATE_LIFT_MENU;
        return;
    }

    g_game.game_tick++;
    g_game.frame_counter++;

    if (g_msg_timer > 0) g_msg_timer--;

    player_update();
    prisoner_update();
    enemies_update();
    bullets_update();
    entities_update_deaths();
    effects_update();
    camera_update();

    // A dynamite blast (triggered from bullets_update) hands control to the
    // explosion cutscene; do not let the checks below clobber that state.
    if (g_game.game_state != STATE_PLAYING) return;

    if (enemies_check_player_collision() && g_player.hit_cooldown == 0) {
        player_take_damage(1);
        g_player.hit_cooldown = 24;
    }

    if (g_player.hits >= 51) {
        g_game.game_state = STATE_DEAD;
        g_death_timer = 0;
        audio_stop_music();
    }

    bool rescue = g_game.dif_level < 3;

    // A rescue mission is lost the moment the ally is killed.
    if (rescue && g_prisoner.present && g_prisoner.state == 2) {
        g_game.game_state = STATE_MISSION_FAILED;
        g_fail_timer = 0;
        audio_stop_music();
        return;
    }

    // Original LOOP (eagle.asm 2173): COMPIT fires once the player reaches the
    // bottom of the map (YOUY >= 0xA2).
    if (g_player.y / TILE_H >= EXTRACT_ROW) {
        if (g_game.dif_level >= 3) {
            // COMPM3: all four detonators of this castle have been set.
            if (map_detonators_complete(g_player.level)) {
                g_game.game_state = STATE_MISSION_COMPLETE;
            }
        } else if (g_prisoner.present && g_prisoner.state == 1) {
            // Rescue: BILLAC (ally following) and unfired (BILLOS clear).
            g_game.game_state = STATE_MISSION_COMPLETE;
        }
    }
}

// Original LIFT menu (eagle.asm 3260-3346): up/down pick a floor of the current
// castle, fire rides the elevator. Selecting the floor reloads it and restarts
// the player at the lift landing.
static void game_update_lift_menu(void) {
    u8 joy = g_input.joy_val;
    u8 pressed = (u8)(joy & ~g_game.lift_prev_joy);
    g_game.lift_prev_joy = joy;

    if (pressed & JOY_UP) {
        if (g_game.lift_floor > 0) g_game.lift_floor--;
    }
    if (pressed & JOY_DOWN) {
        if (g_game.lift_floor < 3) g_game.lift_floor++;
    }
    if (pressed & JOY_FIRE) {
        audio_play_lift();
        int castle = (g_player.level >= 4) ? 1 : 0;
        g_player.level = (u8)(castle * 4 + g_game.lift_floor);
        g_player.xlevel = 0;
        g_game.start_at_entrance = 0;  // arrive at the lift landing, not the entrance
        game_load_floor(g_player.level);
        g_game.game_state = STATE_PLAYING;
    }
}

static void game_update_paused(void) {
    if (g_input.quit_requested) {
        g_input.quit_requested = false;
        g_game.running = false;
        return;
    }
    if (g_input.main_requested) {
        g_input.main_requested = false;
        player_init();
        g_game.paused = false;
        g_game.game_state = STATE_TITLE;
        title_reset();
        audio_start_music();
        return;
    }
    if (g_input.restart_requested) {
        g_input.restart_requested = false;
        game_load_floor(g_player.level);
        g_game.paused = false;
        g_game.game_state = STATE_PLAYING;
        return;
    }
    if (g_input.resume_requested || g_input.pause_requested) {
        g_input.resume_requested = false;
        g_input.pause_requested = false;
        g_game.paused = false;
        g_game.game_state = STATE_PLAYING;
    }
}

// ---- Name entry (original NEWHSC / GETDIG) -------------------------------
// After a qualifying game the original shows a "WELL DONE" plate and asks for
// an eight-character name. Left/right cycle the candidate character (repeating
// while held, one step every five frames), fire commits it; the 0x25 code
// erases the previous character. The table entry is inserted once all eight
// characters are in.
#define NE_DELAY 5  // original GETDIG waits five frames between steps

static int  g_ne_len = 0;       // characters committed so far
static int  g_ne_digit = 1;     // current candidate code (GETDIG d3, 0..0x25)
static int  g_ne_delay = 0;     // frames until the next accepted left/right step
static u8   g_ne_name[HS_NAME_LEN];
static int  g_ne_idle = 0;
static bool g_ne_fire_held = false;
static int  g_ne_done_timer = 0;
// GETDIG walks d3 through 0..0x25 and CHARTBL turns each code into a glyph. The
// resulting cycle is glyphs 0..31, then blank (0x20), then 33..37; the missing
// glyph 32 (digit '5') is a quirk of the original table and is kept as-is.
static u8 name_entry_glyph(int code) {
    if (code == 0x20) return 0;
    return (u8)code;
}


static void name_entry_reset_to_title(void) {
    player_init();
    g_game.game_state = STATE_TITLE;
    title_reset();
    audio_start_music();
}

static void name_entry_begin(void) {
    g_ne_len = 0;
    g_ne_digit = 1;
    g_ne_delay = NE_DELAY;
    memset(g_ne_name, 0, sizeof(g_ne_name));
    g_ne_fire_held = (g_input.joy_val & JOY_FIRE) != 0;
    g_ne_done_timer = 0;
    g_ne_idle = 0;
}

static void game_update_name_entry(void) {
    if (g_ne_done_timer > 0) {
        if (--g_ne_done_timer == 0) name_entry_reset_to_title();
        return;
    }

    u8 joy = g_input.joy_val;

    if (g_ne_delay > 0) {
        g_ne_delay--;
    } else if (joy & JOY_LEFT) {
        g_ne_digit = (g_ne_digit <= 0) ? 0x25 : g_ne_digit - 1;
        g_ne_delay = NE_DELAY;
        g_ne_idle = 0;
    } else if (joy & JOY_RIGHT) {
        g_ne_digit = (g_ne_digit >= 0x25) ? 0 : g_ne_digit + 1;
        g_ne_delay = NE_DELAY;
        g_ne_idle = 0;
    }

    bool fire = (joy & JOY_FIRE) != 0;
    if (fire && !g_ne_fire_held) {
        g_ne_fire_held = true;
        if (g_ne_digit == 0x25) {
            if (g_ne_len > 0) g_ne_len--;
        } else if (g_ne_len < HS_NAME_LEN) {
            g_ne_name[g_ne_len++] = name_entry_glyph(g_ne_digit);
            if (g_ne_len >= HS_NAME_LEN) {
                highscores_insert(g_ne_name, g_player.score);
                g_ne_done_timer = 120;  // hold the finished name before leaving to title
            }
        }
        g_ne_idle = 0;
    }
    if (!fire) g_ne_fire_held = false;

    if (g_ne_len < HS_NAME_LEN) {
        g_ne_idle++;
        if (g_ne_idle > 60 * 10) {
            name_entry_reset_to_title();
        }
    }
}

static void game_update_dead(void) {
    g_death_timer++;
    if (g_death_timer > 120) {
        // Original START0 calls NEWHSC, which asks for a name only when the
        // score reaches the last-placed table entry (HISCORE).
        if (g_player.score > 0 && g_player.score >= highscores_last_score())
            name_entry_begin();
        else {
            g_game.game_state = STATE_GAME_OVER;
            g_game.opt_prev_joy = g_input.joy_val;
            g_game_over_timer = 0;
        }
    }
}

// Original SHTDYN: after the blast the border (colour 0) is cycled through reds
// for as long as the bang plays, then DEAD. We pulse colour 0 the same way.
void game_trigger_explosion(void) {
    if (g_cheats.invulnerability) return;
    g_player.hits = 51;
    g_explosion_timer = 60;
    g_game.game_state = STATE_EXPLOSION;
}

static void game_update_explosion(void) {
    if (g_explosion_timer > 0) g_explosion_timer--;
    if (g_explosion_timer == 0) {
        palette_init();  // drop the red colour 0 before the death screen
        g_death_timer = 0;
        g_game.game_state = STATE_DEAD;
    }
}

static void game_update_game_over(void) {
    u8 joy = g_input.joy_val;
    u8 pressed = (u8)(joy & ~g_game.opt_prev_joy);
    g_game.opt_prev_joy = joy;
    g_game_over_timer++;
    // Auto-return to the title after 10 s so a non-working FIRE input (or a
    // platform where the joystick button is not mapped) can never trap the
    // player on this screen.
    if ((pressed & JOY_FIRE) || g_game_over_timer > 60 * 10) {
        player_init();
        g_game.game_state = STATE_TITLE;
        title_reset();
        audio_start_music();
    }
}

static void game_update_mission_failed(void) {
    g_fail_timer++;
    if (g_fail_timer > 90 && (g_input.joy_val & JOY_FIRE)) {
        player_init();
        g_game.game_state = STATE_TITLE;
        title_reset();
        audio_start_music();
    }
}

static void game_update_mission_complete(void) {
    // Original MSCOMP (eagle.asm 4767): clear HITS/KEYS, refill ammo, then
    // MSNNUM++ and DIFLVL++. DIFLVL 4 ends the game; otherwise the next mission
    // begins at the GROUND floor entrance of the same castle.
    g_player.hits = 0;
    g_player.keys = 0;
    g_player.bullets = 99;
    g_player.xlevel = 0;
    if (g_game.mission_num < 3) g_game.mission_num++;
    g_game.dif_level++;
    if (g_game.dif_level >= 4) {
        g_game.game_state = STATE_GAME_OVER;
        g_game.opt_prev_joy = g_input.joy_val;
        g_game_over_timer = 0;
    } else {
        int castle = (g_player.level >= 4) ? 1 : 0;
        g_player.level = (u8)(castle * 4 + 1);
        g_game.start_at_entrance = 1;
        g_game.game_state = STATE_BRIEFING;
        audio_start_music();  // back to an intro screen: music resumes
        g_briefing_timer = 0;
        g_brief_prev_joy = g_input.joy_val;
    }
}

void game_frame(void) {
    input_poll();

    switch (g_game.game_state) {
        case STATE_TITLE: game_update_title(); break;
        case STATE_MISSION_SELECT: game_update_mission_select(); break;
        case STATE_BRIEFING: game_update_briefing(); break;
        case STATE_PLAYING: game_update_playing(); break;
        case STATE_PAUSED: game_update_paused(); break;
        case STATE_DEAD: game_update_dead(); break;
        case STATE_MISSION_COMPLETE: game_update_mission_complete(); break;
        case STATE_GAME_OVER: game_update_game_over(); break;
        case STATE_MISSION_FAILED: game_update_mission_failed(); break;
        case STATE_LIFT_MENU: game_update_lift_menu(); break;
        case STATE_OPTIONS: game_update_options(); break;
        case STATE_EXPLOSION: game_update_explosion(); break;
        case STATE_NAME_ENTRY: game_update_name_entry(); break;
    }

    renderer_clear(0);

    if (g_game.game_state == STATE_PLAYING || g_game.game_state == STATE_PAUSED ||
        g_game.game_state == STATE_LIFT_MENU || g_game.game_state == STATE_EXPLOSION) {
        palette_init();  // game uses the standard COLS palette
        if (g_game.game_state == STATE_EXPLOSION) {
            // SHTDYN border flash: pulse colour 0 (background) through reds so
            // the walls keep their own colours while the corridors glow red.
            int r = (g_explosion_timer % 16) * 17;
            g_palette.colors[0] = 0xFF000000u | ((u32)r << 16);
            renderer_clear(0);
        }
        map_render_background();
        enemies_draw();
        prisoner_draw();
        bullets_draw();
        player_draw();
        entities_draw_deaths();
        effects_draw();
        // HUD panel: right 64 px column + bottom 8 px strip from panel.raw
        renderer_blit_planar(&g_screen_panel, 256, 0, 320, 200);
        renderer_blit_planar(&g_screen_panel, 0, 192, 320, 200);
    } else if (g_game.game_state == STATE_TITLE) {
        if (g_title_page == TP_MINDSCAPE) {
            screen_palette_apply(&g_screen_ms);
            renderer_blit_planar(&g_screen_ms, 0, 0, 320, 200);
        } else if (g_title_page == TP_TITLE) {
            screen_palette_apply(&g_screen_title);
            renderer_blit_planar(&g_screen_title, 0, 0, 320, 200);
            renderer_draw_string(80, 160, "PRESS FIRE", 0);
        } else {
            // Attract pages: standard palette, CLS + panel, then page art.
            palette_init();
            renderer_blit_planar(&g_screen_panel, 256, 0, 320, 200);
            renderer_blit_planar(&g_screen_panel, 0, 192, 320, 200);
            if (g_title_page == TP_WELCOME) {
                title_draw_welcome();
                title_draw_welcome_guards();
            } else if (g_title_page == TP_HEROES) {
                title_draw_heroes();
            } else if (g_title_page == TP_VALUABLES) {
                title_draw_valuables();
            } else if (g_title_page == TP_SUPPLIES) {
                title_draw_supplies();
            }
        }
    } else if (g_game.game_state == STATE_OPTIONS) {
        palette_init();
        static const char* const labels[OPT_ROWS] = {
            "DIFFICULTY", "CASTLE", "SOUND", "MUSIC", "MESSAGES", "START", "QUIT"
        };
        const char* values[5];
        values[0] = g_game.opt_difficulty ? "HARD" : "EASY";
        values[1] = g_game.castle ? "2" : "1";
        values[2] = audio_is_sfx_enabled() ? "ON" : "OFF";
        values[3] = audio_is_music_enabled() ? "ON" : "OFF";
        values[4] = g_game.opt_messages ? "ON" : "OFF";
        for (int i = 0; i < OPT_ROWS; i++) {
            int y = 40 + i * 20;
            if (i == g_game.opt_cursor) renderer_draw_disc(28, y + 4, 3, 15);
            renderer_draw_string(40, y, labels[i], 0);
            if (i < OPT_START) renderer_draw_string(232, y, values[i], 0);
        }
    } else if (g_game.game_state == STATE_MISSION_SELECT) {
        palette_init();
        // "SELECT" above "LEVEL <n>" (original GETDIF: GTDFS0='SELECT', GTDFS1='LEVEL').
        renderer_draw_string((320 - 6 * TILE_W) / 2, 80, "SELECT", 0);
        char sbuf[12];
        snprintf(sbuf, sizeof(sbuf), "LEVEL %d", 1 + g_game.mission_select);
        renderer_draw_string((320 - (int)strlen(sbuf) * TILE_W) / 2, 104, sbuf, 0);
        renderer_draw_string((320 - 10 * TILE_W) / 2, 152, "PRESS FIRE", 0);
    } else if (g_game.game_state == STATE_BRIEFING) {
        palette_init();
        char buf[48];
        snprintf(buf, sizeof(buf), "MISSION %s", kMissionNames[g_game.mission_num]);
        renderer_draw_string((320 - (int)strlen(buf) * TILE_W) / 2, 72, buf, 0);
        if (g_game.dif_level >= 3) {
            snprintf(buf, sizeof(buf), "BLOW UP THE CASTLE");
        } else {
            snprintf(buf, sizeof(buf), "RESCUE THE PRISONER");
        }
        renderer_draw_string((320 - (int)strlen(buf) * TILE_W) / 2, 104, buf, 0);
    } else if (g_game.game_state == STATE_DEAD) {
        renderer_draw_string(96, 90, "YOU DIED", 0);
    } else if (g_game.game_state == STATE_NAME_ENTRY) {
        palette_init();
        renderer_blit_planar(&g_screen_panel, 256, 0, 320, 200);
        renderer_blit_planar(&g_screen_panel, 0, 192, 320, 200);
        title_wline(4, 1, "WELL DONE");
        title_text(4, 8, "YOU ARE A");
        title_wline(4, 10, "REAL HERO");
        title_wline(1, 18, "ENTER YOUR NAME");
        // HSBLNK: eight bar glyphs where the name is typed.
        for (int i = 0; i < HS_NAME_LEN; i++)
            renderer_draw_char((4 + i) * TILE_W, 22 * TILE_H, 41, 0);
        for (int i = 0; i < g_ne_len; i++)
            renderer_draw_char((4 + i) * TILE_W, 22 * TILE_H, g_ne_name[i], 0);
        if (g_ne_len < HS_NAME_LEN)
            renderer_draw_char((4 + g_ne_len) * TILE_W, 22 * TILE_H,
                               name_entry_glyph(g_ne_digit), 0);
    } else if (g_game.game_state == STATE_GAME_OVER) {
        renderer_draw_string(88, 90, "GAME OVER", 0);
        renderer_draw_string(80, 110, "PRESS FIRE", 0);
    } else if (g_game.game_state == STATE_MISSION_FAILED) {
        renderer_draw_string((320 - 14 * TILE_W) / 2, 84, "MISSION FAILED", 0);
        renderer_draw_string((320 - 20 * TILE_W) / 2, 104, "THE PRISONER IS DEAD", 0);
        renderer_draw_string((320 - 10 * TILE_W) / 2, 128, "PRESS FIRE", 0);
    }

    if (g_game.game_state == STATE_PLAYING || g_game.game_state == STATE_PAUSED) {
        // The transient game message is part of play; hide it while the pause
        // menu is up so it cannot overlap the menu text.
        if (g_msg_timer > 0 && !g_game.paused) {
            renderer_draw_rect(0, 88, 256, 32, 0);
            if (g_msg_found) {
                renderer_draw_string((256 - 9 * TILE_W) / 2, 96, "YOU FOUND", 0);
                renderer_draw_string((256 - (int)strlen(g_msg_text) * TILE_W) / 2, 104, g_msg_text, 0);
            } else {
                renderer_draw_string((256 - (int)strlen(g_msg_text) * TILE_W) / 2, 100, g_msg_text, 0);
            }
        }
        hud_draw();
    }

    if (g_game.game_state == STATE_LIFT_MENU) {
        // Original LIFT9 window at character cells (1,6), 14x12:
        // "SELECT FLOOR" plus BASEMENT/GROUND/FIRST/SECOND and a cursor.
        static const char* const floors[4] = {"BASEMENT", "GROUND", "FIRST", "SECOND"};
        renderer_draw_rect(16, 48, 224, 96, 0);
        renderer_draw_rect(16, 48, 224, 1, 2);
        renderer_draw_rect(16, 143, 224, 1, 2);
        renderer_draw_rect(16, 48, 1, 96, 2);
        renderer_draw_rect(239, 48, 1, 96, 2);
        renderer_draw_string(2 * TILE_W, 7 * TILE_H, "SELECT FLOOR", 0);
        for (int i = 0; i < 4; i++) {
            renderer_draw_string(3 * TILE_W, (9 + i * 2) * TILE_H, floors[i], 0);
        }
        // Original draws the selected floor as a two-tile ball (glyphs 39+40
        // stacked in column 12). Draw it as the same full red disc used by the
        // main menu instead of the top-arc glyph 38 (half ball), placed to the
        // left of the label like the options-menu cursor (text_x - 12).
        renderer_draw_disc(3 * TILE_W - 12,
                           (9 + g_game.lift_floor * 2) * TILE_H + 4, 3, 15);
    }

    // Debug dumps (env EAGLE_DUMP_DIR set)
    static bool dumped_title = false;
    static bool dumped_playing = false;
    static bool dumped_attract[TP_COUNT] = {false};
    if (!dumped_title && g_game.game_state == STATE_TITLE && g_title_timer >= 181) {
        debug_dump_ppm("title");
        dumped_title = true;
    }
    if (g_game.game_state == STATE_TITLE && g_title_page >= TP_WELCOME &&
        !dumped_attract[g_title_page] && g_title_page_t >= 2) {
        char name[24];
        snprintf(name, sizeof(name), "attract_%d", g_title_page);
        debug_dump_ppm(name);
        dumped_attract[g_title_page] = true;
    }
    if (!dumped_playing && (g_game.game_state == STATE_PLAYING || g_game.game_state == STATE_PAUSED)) {
        debug_dump_ppm("playing");
        dumped_playing = true;
    }
    static bool dumped_ne = false;
    if (!dumped_ne && g_game.game_state == STATE_NAME_ENTRY) {
        debug_dump_ppm("nameentry");
        dumped_ne = true;
    }



    platform_present(&g_framebuffer);
    platform_sleep_until_next_frame();
}

void map_render_background(void) {
    // Playfield: 16x24 tiles (256x192 px) from the 96x168 tile buffer.
    // g_scroll_x/y are the camera origin in pixels (world -> screen = world - scroll).
    int sx0 = g_scroll_x / TILE_W;
    int sy0 = g_scroll_y / TILE_H;
    int ox = g_scroll_x % TILE_W;  // 0..15 partial column
    int oy = g_scroll_y % TILE_H;  // 0..7  partial row

    for (int ty = 0; ty < VIEW_H + 1; ty++) {
        for (int tx = 0; tx < VIEW_W + 1; tx++) {
            u8 tile_idx = map_get_tile(sx0 + tx, sy0 + ty);
            if (tile_idx != 0) {
                const Tile* tile = tiles_get(tile_idx);
                if (tile) {
                    renderer_draw_tile(tx * TILE_W - ox, ty * TILE_H - oy, tile, 0);
                }
            }
        }
    }
}

static const u8 g_death_frames[2][6] = {
    {0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9},
    {0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF}
};

void entities_draw_deaths(void) {
    for (int i = 0; i < MAX_DEATH_ANIMS; i++) {
        u8 f = g_deaths[i].frame;
        if (f == 0) continue;
        const u8* g = (f & 1) ? g_death_frames[1] : g_death_frames[0];
        int px = g_deaths[i].x - 16 - g_scroll_x;
        int py = g_deaths[i].y - 12 - g_scroll_y;
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 2; c++) {
                const Tile* tile = tiles_get(g[r * 2 + c]);
                if (tile) renderer_draw_tile(px + c * TILE_W, py + r * TILE_H, tile, 0);
            }
        }
    }
}
