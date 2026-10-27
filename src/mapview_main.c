// Standalone map inspector: scrolls a level's map with the joystick/keys so the
// enemy and item placement can be checked. No HUD; the current view position is
// printed to the console. Reuses the engine modules but never enters the game
// state machine (no game_init / game_frame).
#include "eaglenest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    int level = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            level = atoi(argv[++i]);
        } else if (strncmp(argv[i], "--level=", 8) == 0) {
            level = atoi(argv[i] + 8);
        }
    }
    if (level < 1) level = 1;
    if (level > 7) level = 7;

    if (!platform_init()) {
        return 1;
    }

    memory_init();
    palette_init();
    tiles_init();
    map_init();
    entities_init();
    player_init();
    enemies_init();
    effects_init();
    objects_init();
    map_detonators_init();
    input_init();

    if (!tiles_load_all_assets("assets")) {
        SDL_Log("Warning: could not load all assets");
    }

    // map_load_level also runs objects_place_level (items) and
    // map_erase_detonators; those read g_game.dif_level / g_player.level.
    g_game.dif_level = 3;
    g_player.level = (u8)level;
    map_load_level((u8)level);
    enemies_spawn_from_map();

    int scroll_x = 0;
    int scroll_y = 0;
    const int max_x = MAP_BUFFER_W * TILE_W - VIEW_W * TILE_W;
    const int max_y = MAP_BUFFER_H * TILE_H - VIEW_H * TILE_H;

    int last_tx = -1;
    int last_ty = -1;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                input_gamepad_added();
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                input_gamepad_removed(event.gdevice.which);
            }
        }

        input_poll();
        u8 joy = g_input.joy_val;
        int speed = (joy & JOY_FIRE) ? 24 : 8;
        if (joy & JOY_LEFT)  scroll_x -= speed;
        if (joy & JOY_RIGHT) scroll_x += speed;
        if (joy & JOY_UP)    scroll_y -= speed;
        if (joy & JOY_DOWN)  scroll_y += speed;
        if (scroll_x < 0) scroll_x = 0;
        if (scroll_y < 0) scroll_y = 0;
        if (scroll_x > max_x) scroll_x = max_x;
        if (scroll_y > max_y) scroll_y = max_y;
        renderer_set_scroll(scroll_x, scroll_y);

        renderer_clear(0);
        palette_init();
        map_render_background();
        enemies_draw();
        platform_present(&g_framebuffer);
        platform_sleep_until_next_frame();

        // Print the view-centre tile only when it changes, so the console shows
        // the current location without flooding on every frame.
        int tx = (scroll_x + (VIEW_W * TILE_W) / 2) / TILE_W;
        int ty = (scroll_y + (VIEW_H * TILE_H) / 2) / TILE_H;
        if (tx != last_tx || ty != last_ty) {
            printf("LEVEL %d  X:%d Y:%d\n", level, tx, ty);
            fflush(stdout);
            last_tx = tx;
            last_ty = ty;
        }
    }

    platform_shutdown();
    return 0;
}
