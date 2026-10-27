#ifndef EAGLENEST_H
#define EAGLENEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef SDL_H_MISSING
#include <SDL3/SDL.h>
#else
// Stub types for syntax checking without SDL3
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef struct SDL_AudioSpec { int format; int channels; int freq; } SDL_AudioSpec;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef struct SDL_IOStream SDL_IOStream;
typedef struct { int type; struct { unsigned int which; } gdevice; } SDL_Event;
typedef enum { SDL_INIT_VIDEO=1, SDL_INIT_AUDIO=2, SDL_INIT_GAMEPAD=4 } SDL_InitFlags;
typedef enum { SDL_WINDOW_RESIZABLE=1, SDL_WINDOW_HIGH_PIXEL_DENSITY=2 } SDL_WindowFlags;
typedef enum { SDL_RENDERER_ACCELERATED=1, SDL_RENDERER_PRESENTVSYNC=2 } SDL_RendererFlags;
typedef enum { SDL_LOGICAL_PRESENTATION_INTEGER_SCALE=1 } SDL_LogicalPresentation;
typedef enum { SDL_PIXELFORMAT_ARGB8888=1 } SDL_PixelFormat;
typedef enum { SDL_TEXTUREACCESS_STREAMING=1 } SDL_TextureAccess;
typedef enum { SDL_AUDIO_S16=1 } SDL_AudioFormat;
typedef enum { SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK=1 } SDL_AudioDevice;
typedef enum { SDL_GAMEPAD_BUTTON_DPAD_UP=1, SDL_GAMEPAD_BUTTON_DPAD_DOWN=2, SDL_GAMEPAD_BUTTON_DPAD_LEFT=3, SDL_GAMEPAD_BUTTON_DPAD_RIGHT=4, SDL_GAMEPAD_BUTTON_SOUTH=5, SDL_GAMEPAD_BUTTON_WEST=6, SDL_GAMEPAD_BUTTON_EAST=7, SDL_GAMEPAD_BUTTON_NORTH=8, SDL_GAMEPAD_BUTTON_START=9, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER=10, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER=11 } SDL_GamepadButton;
typedef enum { SDL_GAMEPAD_AXIS_LEFTX=1, SDL_GAMEPAD_AXIS_LEFTY=2, SDL_GAMEPAD_AXIS_RIGHTX=3, SDL_GAMEPAD_AXIS_RIGHTY=4, SDL_GAMEPAD_AXIS_LEFT_TRIGGER=5, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER=6 } SDL_GamepadAxis;
typedef enum { SDL_SCANCODE_UP=1, SDL_SCANCODE_DOWN=2, SDL_SCANCODE_LEFT=3, SDL_SCANCODE_RIGHT=4, SDL_SCANCODE_W=5, SDL_SCANCODE_S=6, SDL_SCANCODE_A=7, SDL_SCANCODE_D=8, SDL_SCANCODE_SPACE=9, SDL_SCANCODE_LCTRL=10, SDL_SCANCODE_RCTRL=11, SDL_SCANCODE_ESCAPE=12 } SDL_Scancode;
typedef enum { SDL_EVENT_QUIT=1, SDL_EVENT_KEY_DOWN=2, SDL_EVENT_WINDOW_RESIZED=3, SDL_EVENT_GAMEPAD_ADDED=4, SDL_EVENT_GAMEPAD_REMOVED=5 } SDL_EventType;
typedef struct { int key; } SDL_Keysym;
typedef struct { SDL_Keysym keysym; } SDL_KeyboardEvent;
#define SDLK_ESCAPE 27
#define SDL_SOFTWARE_RENDERER "software"
#define Uint32 uint32_t
#define SDL_Log(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

#define SCREEN_W 320
#define SCREEN_H 200
#define SCALE_FACTOR 3
#define WINDOW_W (SCREEN_W * SCALE_FACTOR)
#define WINDOW_H (SCREEN_H * SCALE_FACTOR)

#define TILE_W 16
#define TILE_H 8
#define TILE_SIZE (TILE_W * TILE_H)

#define SPRITE_COLS 2
#define SPRITE_ROWS 3
#define SPRITE_W (SPRITE_COLS * TILE_W)
#define SPRITE_H (SPRITE_ROWS * TILE_H)
#define TILE_BYTES 64

#define MAP_TILES_W 24
#define MAP_TILES_H 42
#define MAP_SIZE (MAP_TILES_W * MAP_TILES_H)

#define BLOCK_TILES_W 4
#define BLOCK_TILES_H 4
#define BLOCK_PIXELS_W (BLOCK_TILES_W * TILE_W)
#define BLOCK_PIXELS_H (BLOCK_TILES_H * TILE_H)

#define MAP_FILE_SIZE 1024
#define MAP_BUFFER_W (MAP_TILES_W * BLOCK_TILES_W)
#define MAP_BUFFER_H (MAP_TILES_H * BLOCK_TILES_H)
#define MAP_BUFFER_SIZE (MAP_BUFFER_W * MAP_BUFFER_H)

#define VIEW_W 16
#define VIEW_H 24
#define SCRX_MAX (MAP_BUFFER_W - VIEW_W)
#define SCRY_MAX (MAP_BUFFER_H - VIEW_H)

// Original COMPIT fire row: YOUY >= 0xA2 (bottom of the map) is where a mission
// completes (rescue in tow / all detonators set).
#define EXTRACT_ROW 0xA2

#define MAX_ENEMIES 512
#define MAX_BULLETS 8
#define MAX_DEATH_ANIMS 16
#define MAX_SPARKS 64

#define PALETTE_SIZE 16

#define JOY_UP    (1 << 0)
#define JOY_DOWN  (1 << 1)
#define JOY_LEFT  (1 << 2)
#define JOY_RIGHT (1 << 3)
#define JOY_FIRE  (1 << 4)

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;

typedef struct {
    u32 pixels[SCREEN_H][SCREEN_W];
} Framebuffer;

typedef struct {
    u32 colors[PALETTE_SIZE];
} Palette;

typedef struct {
    u8 data[TILE_BYTES];
} Tile;

typedef struct {
    u8 tiles[MAP_FILE_SIZE];
} Map;

#define PLANAR_W 320
#define PLANAR_H 200
#define PLANAR_SIZE (PLANAR_W * PLANAR_H / 8 * 4)

typedef struct {
    u8 data[PLANAR_SIZE];
    u32 palette[PALETTE_SIZE];
} Screen;

typedef struct {
    int x, y;
    u8 state;
    u8 flags;
    u8 frame;
} Enemy;

typedef struct {
    u8 active;
    int x, y;
    u8 dir;
} Bullet;

// The rescued prisoner/ally ("BILL" in the original). A single entity that
// starts chained on one floor of a rescue mission, then follows the player.
typedef struct {
    u8 present;    // 1 if the prisoner exists on this level
    u8 state;      // 0=chained, 1=following, 2=dead
    int x, y;      // world pixel position of the 2x3 sprite
    int spawn_x, spawn_y;
    u8 frame;      // BLLCHR frame 0..7 (dir | anim)
    u8 dir;        // facing 0=down,1=up,2=right,3=left
    int anim;      // animation toggle
    int step_delay;// 0/1 throttle so the ally moves at half the player's speed
    int stun;      // >0: frozen, waiting to be woken by a warning shot
    int blocked;   // consecutive blocked frames while following
    int lost;      // frames spent off-screen while following
} Prisoner;

typedef struct {
    int x, y;
    u8 frame;
} DeathAnim;

typedef struct {
    int x, y;
    int old_x, old_y;
    u8 frame;
    u8 old_frame;
    u8 bullets;
    u8 hits;
    u8 keys;
    u32 score;
    u8 level;
    u8 xlevel;
    u8 bill_x, bill_y;
    u8 bill_level;
    u8 bill_active;
    u8 bill_speed;
    u8 bill_frame;
    u8 bill_flags;
    u8 has_pass;
    u8 hit_cooldown;
} Player;

typedef struct {
    u8 joy_val;
    bool pause_requested;
    bool resume_requested;
    bool restart_requested;
    bool main_requested;
    bool quit_requested;
} Input;

typedef struct {
    uint32_t seed;
    uint32_t frame_counter;
    uint32_t game_tick;
    bool running;
    bool paused;
    u8 game_state;
    // Mission selection (original GETDIF / DIFLVL / MSNNUM).
    u8 mission_select;  // 0..3 chosen on the LEVEL SELECT screen
    u8 dif_level;       // original DIFLVL: >=3 = blow up the castle, <3 = rescue
    u8 mission_num;     // original MSNNUM: mission brief sequence index
    // Elevator (original LIFT, "SELECT FLOOR"): floor menu within the castle.
    u8 lift_floor;          // 0=BASEMENT .. 3=SECOND (the highlighted entry)
    u8 lift_pending;        // set by objects.c when the player steps on the lift
    u8 lift_prev_joy;       // rising-edge tracker for the lift menu
    u8 bill_lift;           // original BILLLF: the ally rides the lift with the player
    u8 start_at_entrance;   // original XLEVEL==0xFE: spawn at (47,161), else (10,151)
    // Pre-game options menu (original CTRLMNU).
    u8 opt_cursor;          // 0=DIFFICULTY 1=CASTLE 2=SOUND 3=MUSIC 4=MESSAGES 5=START 6=QUIT
    u8 opt_prev_joy;        // rising-edge tracker for the options menu
    u8 opt_difficulty;      // original DFCLTY: 0=EASY (one-hit kills) 1=HARD (two hits)
    u8 castle;              // original LOADED: 0=castle 1, 1=castle 2 (maps 4..7)
    u8 opt_messages;        // original MSFLG: show object pick-up/status pop-ups
} GameState;

typedef struct {
    int start_level;
    bool invulnerability;
    bool alldoorsopen;
    bool unlimitamo;
} Cheats;

extern Cheats g_cheats;
void cheats_parse_args(int argc, char* const* argv);
void cheats_print_usage(const char* prog);

extern GameState g_game;
extern Player g_player;
extern Prisoner g_prisoner;
extern Enemy g_enemies[MAX_ENEMIES];
extern Bullet g_bullets[MAX_BULLETS];
extern DeathAnim g_deaths[MAX_DEATH_ANIMS];
extern Input g_input;
extern Framebuffer g_framebuffer;
extern Palette g_palette;
extern Tile g_tileset[256];
extern Tile g_font[46];
extern Map g_maps[8];
extern u8 g_map_buffer[MAP_BUFFER_SIZE];
extern u8 g_block_data[128 * 16];
extern int g_scroll_x;
extern int g_scroll_y;
extern Screen g_screen_ms;
extern Screen g_screen_title;
extern Screen g_screen_panel;

bool game_in_gameplay(void);
void game_init(void);
void game_frame(void);
void game_shutdown(void);
// Lethal dynamite blast: red colour-0 flash on the playfield, then death.
void game_trigger_explosion(void);

void memory_init(void);
void memory_reset_game(void);
uint32_t random_next(void);
u8 random_byte(void);
u8 random_range(u8 min, u8 max);

void palette_init(void);
void palette_set_color(u8 index, u32 color);
void palette_load_from_amiga(const u16* amiga_colors, int count);
u32 palette_get(u8 index);
void palette_black(void);
void palette_white(void);

void renderer_init(void);
void renderer_clear(u8 color_index);
void renderer_set_scroll(int x, int y);
void renderer_draw_tile(int screen_x, int screen_y, const Tile* tile, u8 palette_offset);
u8 renderer_char_to_glyph(char c);
void renderer_draw_char(int screen_x, int screen_y, u8 char_index, u8 palette_offset);
void renderer_draw_string(int x, int y, const char* str, u8 palette_offset);
void renderer_draw_rect(int x, int y, int w, int h, u8 color_index);
void renderer_draw_disc(int cx, int cy, int radius, u8 color_index);
bool screen_load_planar(const char* base_path, const char* stem, Screen* s, bool has_palette_header);
void screen_palette_apply(const Screen* s);
void renderer_blit_planar(const Screen* s, int x0, int y0, int x1, int y1);

void tiles_init(void);
bool tiles_load_tileset(const char* path);
bool tiles_load_font(const char* path);
bool tiles_load_blocks(const char* path);
bool tiles_load_map(int map_index, const char* path);
bool tiles_load_all_assets(const char* base_path);
const Tile* tiles_get(u8 index);
const Tile* tiles_get_font(u8 index);
u8 tiles_get_block_tile(u8 block_idx, int tile_x, int tile_y);
void tiles_decode_amiga_tile(const u8* src_planes[4], Tile* dst);

void input_init(void);
void input_shutdown(void);
void input_poll(void);
void input_gamepad_added(void);
void input_gamepad_removed(u32 instance_id);
bool input_is_pressed(u8 mask);
u8 input_get_joyval(void);

void audio_init(void);
void audio_mix_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);
void audio_shutdown(void);
bool audio_load_assets(const char* base_path);
void audio_play_sfx(int channel, int16_t* data, int len, bool loop);
void audio_play_step_at(int src_x, int src_y, int listen_x, int listen_y);
void audio_play_gun(void);
void audio_play_clank(void);
void audio_play_bang(void);
void audio_play_pickup(void);
void audio_play_lift(void);
void audio_play_locked(void);
void audio_start_music(void);
void audio_stop_music(void);
void audio_toggle_music(void);
bool audio_is_music_enabled(void);
void audio_toggle_sfx(void);
bool audio_is_sfx_enabled(void);
void audio_set_master_volume(float vol);

void map_init(void);
void map_load_level(u8 level);
u8 map_get_tile(int tile_x, int tile_y);
void map_set_tile(int tile_x, int tile_y, u8 tile);
bool map_is_solid(int tile_x, int tile_y);
bool map_is_enemy_solid(int tile_x, int tile_y);
bool map_line_of_sight(int x0, int y0, int x1, int y1);
bool map_is_door(int tile_x, int tile_y);
bool map_break_wood_door(int tile_x, int tile_y);
bool map_shot_officer(int tile_x, int tile_y, u8 tile);
bool map_is_enemy_spawn(int tile_x, int tile_y);
void map_clear_tile(int tile_x, int tile_y);
int map_world_to_tile_x(int world_x);
int map_world_to_tile_y(int world_y);

// Detonators (original DETTBL / ERADET / SHTDET/SHTDET): one blast wall per
// floor on a "blow up the castle" mission (DIFLVL == 3). Destroying all four of
// the current castle and reaching the bottom of the map completes the mission.
void map_detonators_init(void);
void map_erase_detonators(u8 level);
bool map_detonator_hit(int tile_x, int tile_y);
bool map_detonators_complete(u8 level);

void entities_init(void);
int entities_find_free_enemy(void);
int entities_find_free_bullet(void);
int entities_find_free_death(void);
void entities_spawn_enemy(int x, int y, u8 type);
void entities_spawn_bullet(int x, int y, u8 dir, bool player_bullet);
void entities_spawn_death(int x, int y);
void entities_update_deaths(void);
bool entities_check_collision(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2);

void player_init(void);
void player_start_level(void);
void player_update(void);
void player_fire(void);
void player_draw(void);
void player_take_damage(int amount);
void player_kill(void);
void player_add_score(int points);
void player_add_keys(int count);

void prisoner_init(void);
void prisoner_place_level(u8 level);
void prisoner_update(void);
void prisoner_draw(void);
bool prisoner_bullet(int bx, int by);
bool prisoner_rescued(void);

void enemies_init(void);
void enemies_spawn_from_map(void);
void enemies_update(void);
void enemies_draw(void);
void enemies_draw_sprite_at(int x, int y, u8 sprite);
bool enemies_occupy_tile(int tx, int ty);
bool enemies_check_player_collision(void);
bool enemies_damage_at(int x, int y);

void bullets_init(void);
void bullets_update(void);
void bullets_draw(void);

void effects_init(void);
void effects_update(void);
void effects_draw(void);
void effects_spawn_sparks(int x, int y);

void hud_draw(void);

extern const int g_objtbl0_count;
extern const int g_objtbl1_count;
extern const u8 g_objtbl0[112][7];
extern const u8 g_objtbl1[98][7];
extern const u8 g_obchtb[9][4];

extern int g_msg_timer;
extern int g_msg_found;
extern char g_msg_text[24];

void objects_init(void);
void objects_place_level(u8 level);
void objects_player_touch(void);
void objects_show_message(const char* text);
bool objects_try_open_door_col(int col, int y, int h);
bool objects_try_open_door_row(int row, int x, int w);
void objects_open_at(int tx, int ty);
bool map_tile_walkable(u8 tile);

// ---- High scores (original HSCTBL / NEWHSC / TTL1) -----------------------
// Nine "heroes" (the original table is 8 fixed records plus the working entry
// at lbB0016A0/HISCORE). Names are stored as font glyph indices, exactly like
// the original CHARTBL codes, so they can be drawn straight to the screen.
#define HS_ENTRIES 9
#define HS_NAME_LEN 8
typedef struct {
    u8 name[HS_NAME_LEN];
    u32 score;
} HighScore;
extern HighScore g_highscores[HS_ENTRIES];
void highscores_init(void);
bool highscores_load(void);
void highscores_save(void);
u32  highscores_last_score(void);
void highscores_insert(const u8 name[HS_NAME_LEN], u32 score);

// Platform functions
bool platform_init(void);
void platform_shutdown(void);
void platform_present(Framebuffer* fb);
void platform_audio_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);
void platform_sleep_until_next_frame(void);
uint32_t platform_get_ticks(void);

// Game internal functions
void map_render_background(void);
void entities_draw_deaths(void);

#ifndef SDL_H_MISSING
typedef struct SDL_Rect SDL_Rect;
#else
typedef struct { int x, y, w, h; } SDL_Rect;
#endif

#endif