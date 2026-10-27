#include "eaglenest.h"

int main(int argc, char* argv[]) {
    cheats_parse_args(argc, argv);

    if (!platform_init()) {
        return 1;
    }

    game_init();

    while (g_game.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                g_game.running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) {
                    g_input.pause_requested = true;
                } else if (event.key.key == SDLK_M && (event.key.mod & SDL_KMOD_ALT)) {
                    audio_toggle_music();
                    // Music belongs to the intro/brief screens only: never let
                    // the toggle start it while a level is in progress.
                    if (game_in_gameplay()) {
                        audio_stop_music();
                    }
                    objects_show_message(audio_is_music_enabled() ? "MUSIC ON" : "MUSIC OFF");
                } else if (event.key.key == SDLK_S && (event.key.mod & SDL_KMOD_ALT)) {
                    audio_toggle_sfx();
                    objects_show_message(audio_is_sfx_enabled() ? "SOUND ON" : "SOUND OFF");
                } else if (g_game.paused && event.key.key == SDLK_R) {
                    g_input.resume_requested = true;
                } else if (g_game.paused && event.key.key == SDLK_L) {
                    g_input.restart_requested = true;
                } else if (g_game.paused && event.key.key == SDLK_M) {
                    g_input.main_requested = true;
                } else if (g_game.paused && event.key.key == SDLK_Q) {
                    g_input.quit_requested = true;
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                input_gamepad_added();
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                input_gamepad_removed(event.gdevice.which);
            } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                // Handled by SDL_SetRenderLogicalPresentation
            }
        }

        game_frame();
    }

    game_shutdown();
    return 0;
}