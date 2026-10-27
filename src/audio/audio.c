#include "eaglenest.h"
#include "audio/mod.h"
#include <math.h>

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BUFFER_SIZE 4096

#define AUDIO_CHANNELS 4
#define STEP_CHANNEL 3  // dedicated so a footstep never cuts a gun/bang on SFX

typedef struct {
    int16_t* data;
    int length;
    int pos;
    bool playing;
    bool loop;
    float volume;
    float pan;      // -1 = hard left, 0 = centre, +1 = hard right
} SoundChannel;

static SoundChannel g_channels[AUDIO_CHANNELS] = {0};
static int16_t g_mix_buffer[AUDIO_BUFFER_SIZE * 2] = {0};  // interleaved stereo
static float g_master_volume = 1.0f;

static int16_t* g_sfx_gun = NULL;
static int g_sfx_gun_len = 0;
static int16_t* g_sfx_clank = NULL;
static int g_sfx_clank_len = 0;
static int16_t* g_sfx_bang = NULL;
static int g_sfx_bang_len = 0;
static int16_t* g_music_0 = NULL;  // whole intro module rendered to PCM
static int g_music_0_len = 0;

static int16_t* g_sfx_pickup = NULL;
static int g_sfx_pickup_len = 0;
static int16_t* g_sfx_lift = NULL;
static int g_sfx_lift_len = 0;
static int16_t* g_sfx_locked = NULL;
static int g_sfx_locked_len = 0;
static int16_t* g_sfx_step = NULL;
static int g_sfx_step_len = 0;

static int g_music_channel = -1;
static int g_music_buffer = 0;
static bool g_music_enabled = true;
static bool g_sfx_enabled = true;

void audio_init(void) {
    memset(g_channels, 0, sizeof(g_channels));
    for (int i = 0; i < 4; i++) {
        g_channels[i].volume = 1.0f;
    }
}

void audio_shutdown(void) {
    // Stop all channels first
    for (int i = 0; i < 4; i++) {
        g_channels[i].playing = false;
        g_channels[i].data = NULL;
    }
    // Free the globally owned audio buffers
    free(g_sfx_gun);
    free(g_sfx_clank);
    free(g_sfx_bang);
    free(g_music_0);
    free(g_sfx_pickup);
    free(g_sfx_lift);
    free(g_sfx_locked);
    free(g_sfx_step);
    g_sfx_gun = g_sfx_clank = g_sfx_bang = g_music_0 = NULL;
    g_sfx_pickup = g_sfx_lift = g_sfx_locked = g_sfx_step = NULL;
}

// Procedurally generate a short int16 tone. The pitch glides from f0 to f1,
// decay_k controls how quickly the volume drops, and square selects a harsh
// (buzzy) waveform instead of a sine with a couple of overtones.
static int16_t* synth_tone(int ms, float f0, float f1, float vol, float decay_k, bool square, int* out_len) {
    int n = AUDIO_SAMPLE_RATE * ms / 1000;
    *out_len = n;
    if (n <= 0) return NULL;

    int16_t* buf = malloc((size_t)n * sizeof(int16_t));
    if (!buf) return NULL;

    const float pi = 3.14159265358979323846f;
    double phase = 0.0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        float f = f0 + (f1 - f0) * t;
        phase += 2.0 * pi * f / AUDIO_SAMPLE_RATE;
        float env = expf(-decay_k * t);
        float atk = (t < 0.02f) ? (t / 0.02f) : 1.0f;
        float s;
        if (square) {
            s = (sinf((float)phase) >= 0.0f) ? 0.6f : -0.6f;
        } else {
            s = sinf((float)phase)
                + 0.35f * sinf((float)(2.0 * phase))
                + 0.15f * sinf((float)(3.0 * phase));
            s /= 1.5f;
        }
        buf[i] = (int16_t)(vol * 32767.0f * env * atk * s);
    }
    return buf;
}

static int16_t* load_raw_audio(const char* path, int* out_len) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return NULL;

    int64_t size = SDL_GetIOSize(io);
    if (size <= 0) {
        SDL_CloseIO(io);
        return NULL;
    }

    int16_t* data = malloc((size_t)size);
    if (!data) {
        SDL_CloseIO(io);
        return NULL;
    }

    size_t read = SDL_ReadIO(io, data, (size_t)size);
    SDL_CloseIO(io);

    if (read != (size_t)size) {
        free(data);
        return NULL;
    }

    *out_len = (int)(size / sizeof(int16_t));
    return data;
}

// Render a ProTracker module (.mod) to a flat mono PCM loop with the built-in
// decoder (src/audio/mod.c), so the rest of the mixer can treat it like any
// other sample. This keeps the game's only external dependency SDL3.
static bool load_music_module(const char* path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) {
        SDL_Log("Music: cannot open %s: %s", path, SDL_GetError());
        return false;
    }

    int frames = 0;
    int16_t* buf = mod_render_pcm(data, size, AUDIO_SAMPLE_RATE, &frames);
    SDL_free(data);

    if (!buf || frames <= 0) {
        SDL_Log("Music: could not decode module %s", path);
        free(buf);
        return false;
    }

    g_music_0 = buf;
    g_music_0_len = frames;
    return true;
}

bool audio_load_assets(const char* base_path) {
    char path[256];

    snprintf(path, sizeof(path), "%s/gun.raw", base_path);
    g_sfx_gun = load_raw_audio(path, &g_sfx_gun_len);

    snprintf(path, sizeof(path), "%s/clank.raw", base_path);
    g_sfx_clank = load_raw_audio(path, &g_sfx_clank_len);

    snprintf(path, sizeof(path), "%s/bang.raw", base_path);
    g_sfx_bang = load_raw_audio(path, &g_sfx_bang_len);

    snprintf(path, sizeof(path), "%s/music/633_fjord_attack.mod", base_path);
    load_music_module(path);

    // Synthesiser effects not present in the original sample set.
    g_sfx_pickup = synth_tone(110, 900.0f, 1500.0f, 0.85f, 5.0f, false, &g_sfx_pickup_len);
    g_sfx_lift   = synth_tone(650, 260.0f, 90.0f, 0.65f, 1.6f, true,  &g_sfx_lift_len);
    g_sfx_locked = synth_tone(170, 150.0f, 100.0f, 0.65f, 2.5f, true,  &g_sfx_locked_len);

    // A short, muffled "tap" used for footsteps (player and enemies). Low and
    // heavily damped so it reads as quiet movement rather than an event.
    g_sfx_step = synth_tone(45, 200.0f, 80.0f, 0.5f, 16.0f, false, &g_sfx_step_len);

    return true;
}

void audio_play_sfx(int channel, int16_t* data, int len, bool loop) {
    if (channel < 0 || channel >= AUDIO_CHANNELS) return;
    if (!data || len <= 0) return;

    g_channels[channel].data = data;
    g_channels[channel].length = len;
    g_channels[channel].pos = 0;
    g_channels[channel].playing = true;
    g_channels[channel].loop = loop;
    g_channels[channel].pan = 0.0f;
    g_channels[channel].volume = 1.0f;
}

// Play a footstep "as heard" from the listener: quieter with distance and panned
// to the source's horizontal offset, so a moving enemy can be placed by ear.
// Footsteps beyond AUDIBLE_RANGE are dropped entirely.
#define STEP_AUDIBLE_RANGE 300.0f
void audio_play_step_at(int src_x, int src_y, int listen_x, int listen_y) {
    if (!g_sfx_enabled || !g_sfx_step) return;

    float dx = (float)(src_x - listen_x);
    float dy = (float)(src_y - listen_y);
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist >= STEP_AUDIBLE_RANGE) return;

    float atten = 1.0f - (dist / STEP_AUDIBLE_RANGE);
    float pan = dx / 160.0f;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;

    audio_play_sfx(STEP_CHANNEL, g_sfx_step, g_sfx_step_len, false);
    g_channels[STEP_CHANNEL].volume = atten * atten;  // quadratic falloff
    g_channels[STEP_CHANNEL].pan = pan;
}

#define SFX_CHANNEL 2

void audio_play_gun(void) {
    if (!g_sfx_enabled) return;
    audio_play_sfx(SFX_CHANNEL, g_sfx_gun, g_sfx_gun_len, false);
}

void audio_play_clank(void) {
    if (!g_sfx_enabled) return;
    audio_play_sfx(SFX_CHANNEL, g_sfx_clank, g_sfx_clank_len, false);
}

void audio_play_bang(void) {
    if (!g_sfx_enabled) return;
    audio_play_sfx(SFX_CHANNEL, g_sfx_bang, g_sfx_bang_len, false);
}

void audio_play_pickup(void) {
    if (!g_sfx_enabled) return;
    audio_play_sfx(SFX_CHANNEL, g_sfx_pickup, g_sfx_pickup_len, false);
}

void audio_play_lift(void) {
    if (!g_sfx_enabled) return;
    audio_play_sfx(SFX_CHANNEL, g_sfx_lift, g_sfx_lift_len, false);
}

void audio_play_locked(void) {
    if (!g_sfx_enabled) return;
    audio_play_sfx(SFX_CHANNEL, g_sfx_locked, g_sfx_locked_len, false);
}

void audio_start_music(void) {
    if (!g_music_enabled) return;
    if (!g_music_0 || g_music_0_len <= 0) return;
    g_music_channel = 0;
    g_music_buffer = 0;
    audio_play_sfx(0, g_music_0, g_music_0_len, true);
}

void audio_stop_music(void) {
    g_channels[0].playing = false;
    g_channels[1].playing = false;
    g_music_channel = -1;
}

void audio_toggle_music(void) {
    g_music_enabled = !g_music_enabled;
    if (g_music_enabled) {
        audio_start_music();
    } else {
        audio_stop_music();
    }
}

bool audio_is_music_enabled(void) {
    return g_music_enabled;
}

void audio_toggle_sfx(void) {
    g_sfx_enabled = !g_sfx_enabled;
    if (!g_sfx_enabled) {
        g_channels[SFX_CHANNEL].playing = false;
    }
}

bool audio_is_sfx_enabled(void) {
    return g_sfx_enabled;
}

void audio_set_master_volume(float vol) {
    g_master_volume = vol < 0 ? 0 : (vol > 1 ? 1 : vol);
}

void audio_mix_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount) {
    (void)userdata;
    (void)stream;
    (void)total_amount;

    // Stereo: additional_amount is bytes; each frame is two int16 samples.
    int frames = additional_amount / (int)(sizeof(int16_t) * 2);
    if (frames > AUDIO_BUFFER_SIZE) frames = AUDIO_BUFFER_SIZE;
    if (frames <= 0) return;

    memset(g_mix_buffer, 0, (size_t)frames * 2 * sizeof(int16_t));

    for (int ch = 0; ch < AUDIO_CHANNELS; ch++) {
        SoundChannel* c = &g_channels[ch];
        if (!c->playing || !c->data) continue;

        // Equal-power-ish pan: a centred sound stays at full level on both ears.
        float pan = c->pan;
        float gl = (pan > 0.0f) ? (1.0f - pan) : 1.0f;
        float gr = (pan < 0.0f) ? (1.0f + pan) : 1.0f;

        for (int i = 0; i < frames; i++) {
            if (c->pos >= c->length) {
                if (c->loop) {
                    c->pos = 0;
                } else {
                    c->playing = false;
                    break;
                }
            }

            float s = c->data[c->pos] * c->volume * g_master_volume;
            int32_t l = (int32_t)(s * gl) + g_mix_buffer[i * 2];
            int32_t r = (int32_t)(s * gr) + g_mix_buffer[i * 2 + 1];
            g_mix_buffer[i * 2]     = (int16_t)SDL_clamp(l, -32768, 32767);
            g_mix_buffer[i * 2 + 1] = (int16_t)SDL_clamp(r, -32768, 32767);
            c->pos++;
        }
    }

    SDL_PutAudioStreamData(stream, g_mix_buffer, frames * 2 * (int)sizeof(int16_t));
}