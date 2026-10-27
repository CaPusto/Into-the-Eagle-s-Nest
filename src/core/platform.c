#include "eaglenest.h"

static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static SDL_Texture* g_texture = NULL;
static SDL_AudioStream* g_audio_stream = NULL;
static uint64_t g_perf_freq = 0;
static uint64_t g_last_frame = 0;

const char* WINDOW_TITLE = "Into The Eagle's Nest - SDL3 Port";

bool platform_init(void) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    g_window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_W, WINDOW_H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    g_renderer = SDL_CreateRenderer(g_window, NULL);
    if (!g_renderer) {
        // No usable GPU/accelerated driver (VMs, headless, broken EGL): fall
        // back to the software renderer so the game still runs.
        SDL_Log("SDL_CreateRenderer (default) failed: %s; trying software", SDL_GetError());
        g_renderer = SDL_CreateRenderer(g_window, SDL_SOFTWARE_RENDERER);
    }
    if (!g_renderer) {
        SDL_Log("SDL_CreateRenderer (software) failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(g_renderer, 1);

    SDL_SetRenderLogicalPresentation(g_renderer, SCREEN_W, SCREEN_H,
        SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!g_texture) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }

    SDL_AudioSpec spec = {0};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 44100;

    g_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_mix_callback, NULL);
    if (!g_audio_stream) {
        SDL_Log("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }

    SDL_ResumeAudioStreamDevice(g_audio_stream);

    g_perf_freq = SDL_GetPerformanceFrequency();
    g_last_frame = SDL_GetPerformanceCounter();

    return true;
}

void platform_shutdown(void) {
    if (g_audio_stream) SDL_DestroyAudioStream(g_audio_stream);
    if (g_texture) SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

void platform_present(Framebuffer* fb) {
    void* pixels;
    int pitch;
    SDL_LockTexture(g_texture, NULL, &pixels, &pitch);
    SDL_memcpy(pixels, fb->pixels, SCREEN_W * SCREEN_H * sizeof(u32));
    SDL_UnlockTexture(g_texture);

    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
}

void platform_audio_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount) {
    (void)userdata;
    (void)stream;
    (void)additional_amount;
    (void)total_amount;
}

void platform_sleep_until_next_frame(void) {
    const uint64_t target_ticks = g_perf_freq / 50;
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t elapsed = now - g_last_frame;

    if (elapsed < target_ticks) {
        uint64_t sleep_ms = ((target_ticks - elapsed) * 1000) / g_perf_freq;
        if (sleep_ms > 1) {
            SDL_Delay((Uint32)sleep_ms - 1);
        }
        while ((SDL_GetPerformanceCounter() - g_last_frame) < target_ticks) {
            SDL_Delay(0);
        }
    }
    g_last_frame = SDL_GetPerformanceCounter();
}

uint32_t platform_get_ticks(void) {
    return (uint32_t)((SDL_GetPerformanceCounter() * 1000) / g_perf_freq);
}