#include "eaglenest.h"

Cheats g_cheats = { 1, false, false, false };

// The game ships 8 maps (0..7); a new game starts on level 1 and the run ends
// when the level counter reaches 8, so the last startable level is 7.
static int clamp_level(int lvl) {
    if (lvl < 1) return 1;
    if (lvl > 7) return 7;
    return lvl;
}

void cheats_print_usage(const char* prog) {
    SDL_Log("Usage: %s [options]", prog ? prog : "eaglenest");
    SDL_Log("  --level N          start on level N (1-7)");
    SDL_Log("  --invulnerability  player cannot be hurt");
    SDL_Log("  --alldoorsopen     metal doors open without a key");
    SDL_Log("  --unlimitamo       always keep a full weapon load");
}

void cheats_parse_args(int argc, char* const* argv) {
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--level") == 0) {
            if (i + 1 < argc) {
                g_cheats.start_level = clamp_level(atoi(argv[++i]));
            } else {
                SDL_Log("--level requires a value (1-7)");
            }
        } else if (strncmp(a, "--level=", 8) == 0) {
            g_cheats.start_level = clamp_level(atoi(a + 8));
        } else if (strcmp(a, "--invulnerability") == 0) {
            g_cheats.invulnerability = true;
        } else if (strcmp(a, "--alldoorsopen") == 0) {
            g_cheats.alldoorsopen = true;
        } else if (strcmp(a, "--unlimitamo") == 0) {
            g_cheats.unlimitamo = true;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            cheats_print_usage(argv[0]);
            exit(0);
        }
    }
}
