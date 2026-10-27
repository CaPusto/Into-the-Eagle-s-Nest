#include "eaglenest.h"
#include <stdlib.h>
#include <string.h>

GameState g_game = {0};
Player g_player = {0};
Enemy g_enemies[MAX_ENEMIES] = {0};
Bullet g_bullets[MAX_BULLETS] = {0};
DeathAnim g_deaths[MAX_DEATH_ANIMS] = {0};
Input g_input = {0};
Framebuffer g_framebuffer = {0};
Palette g_palette = {0};
Tile g_tileset[256] = {0};
Tile g_font[46] = {0};
Map g_maps[8] = {0};
u8 g_map_buffer[MAP_BUFFER_SIZE] = {0};
u8 g_block_data[128 * 16] = {0};
Screen g_screen_ms = {0};
Screen g_screen_title = {0};
Screen g_screen_panel = {0};

static uint32_t lcg_seed = 0x12345678;

void memory_init(void) {
    memset(&g_game, 0, sizeof(GameState));
    memset(&g_player, 0, sizeof(Player));
    memset(g_enemies, 0, sizeof(g_enemies));
    memset(g_bullets, 0, sizeof(g_bullets));
    memset(g_deaths, 0, sizeof(g_deaths));
    memset(&g_input, 0, sizeof(Input));
    memset(&g_framebuffer, 0, sizeof(Framebuffer));
    memset(&g_palette, 0, sizeof(Palette));
    memset(g_tileset, 0, sizeof(g_tileset));
    memset(g_font, 0, sizeof(g_font));
    memset(g_maps, 0, sizeof(g_maps));
    memset(g_map_buffer, 0, sizeof(g_map_buffer));
    memset(g_block_data, 0, sizeof(g_block_data));

    g_game.seed = lcg_seed;
    g_game.running = true;
    g_game.game_state = 0;
}

void memory_reset_game(void) {
    g_player.x = 160;
    g_player.y = 100;
    g_player.old_x = 160;
    g_player.old_y = 100;
    g_player.frame = 0;
    g_player.old_frame = 0;
    g_player.bullets = 0x99;
    g_player.hits = 0;
    g_player.keys = 0;
    g_player.score = 0;
    g_player.level = 1;
    g_player.xlevel = 0;
    g_player.bill_x = 0;
    g_player.bill_y = 0;
    g_player.bill_level = 0;
    g_player.bill_active = 0;
    g_player.bill_speed = 0;
    g_player.bill_frame = 0;
    g_player.bill_flags = 0;

    memset(g_enemies, 0, sizeof(g_enemies));
    memset(g_bullets, 0, sizeof(g_bullets));
    memset(g_deaths, 0, sizeof(g_deaths));
}

uint32_t random_next(void) {
    g_game.seed = g_game.seed * 0xBB40E62D + 1;
    return g_game.seed;
}

u8 random_byte(void) {
    return (u8)(random_next() >> 24);
}

u8 random_range(u8 min, u8 max) {
    return min + (random_byte() % (max - min + 1));
}