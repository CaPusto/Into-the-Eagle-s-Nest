#include "mod.h"

#include <stdlib.h>
#include <string.h>

#define MOD_MAX_CHANNELS 8
#define MOD_NOTES 36  // 3 octaves

// Amiga Paula clock (PAL). Output frequency = MOD_CLOCK / (2 * period).
#define MOD_CLOCK 7093789.2

// Finetune-0 period table (C-1 .. B-3). All instruments in the game's module
// use finetune 0, so a single table is enough.
static const int k_period[MOD_NOTES] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
};

typedef struct {
    const int8_t* data;  // points into the loaded file
    int length;          // bytes
    int loop_start;      // bytes
    int loop_length;     // bytes; 0 = one-shot (no loop)
    int volume;          // 0..64
} ModSample;

typedef struct {
    const ModSample* sample;  // current instrument, or NULL when silent
    double pos;               // fractional position in sample frames
    int period;
    int base_note;            // index into k_period for arpeggio
    int volume;               // 0..64
    int arp_x, arp_y;         // arpeggio semitone offsets (0 = inactive)
} ModChannel;

typedef struct {
    ModSample samples[31];
    const uint8_t* patterns;
    int nchannels;
    int npatterns;
    uint8_t order[128];
    int song_length;
    int restart;
} ModFile;

typedef struct {
    int16_t* p;
    size_t len;
    size_t cap;
} Buf;

static int buf_reserve(Buf* b, size_t extra) {
    if (b->len + extra <= b->cap) return 1;
    size_t nc = b->cap ? b->cap : 65536;
    while (nc < b->len + extra) nc *= 2;
    int16_t* np = realloc(b->p, nc * sizeof(int16_t));
    if (!np) return 0;
    b->p = np;
    b->cap = nc;
    return 1;
}

// Map a period from the file to the nearest note index (for arpeggio).
static int period_to_note(int period) {
    int best = 0, bestd = 1 << 30;
    for (int i = 0; i < MOD_NOTES; i++) {
        int d = k_period[i] - period;
        if (d < 0) d = -d;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

static void process_row(const ModFile* mf, ModChannel* ch, int pattern, int row,
                        int* speed, int* tempo, int* jump_order) {
    const uint8_t* d = mf->patterns +
                       (size_t)pattern * 64 * mf->nchannels * 4 +
                       (size_t)row * mf->nchannels * 4;

    // Arpeggio is a per-row effect: clear it before reading this row.
    for (int c = 0; c < mf->nchannels; c++) {
        ch[c].arp_x = ch[c].arp_y = 0;
    }

    for (int c = 0; c < mf->nchannels; c++) {
        const uint8_t* n = d + c * 4;
        int sample = (n[0] & 0xF0) | (n[2] >> 4);
        int period = ((n[0] & 0x0F) << 8) | n[1];
        int cmd = n[2] & 0x0F;
        int data = n[3];
        ModChannel* cc = &ch[c];

        if (sample >= 1 && sample <= 31) {
            const ModSample* s = &mf->samples[sample - 1];
            cc->sample = s;
            cc->pos = 0.0;
            cc->volume = s->volume;
        }
        if (period) {
            cc->period = period;
            cc->base_note = period_to_note(period);
        }

        switch (cmd) {
            case 0x0:  // arpeggio
                if (data) {
                    cc->arp_x = data >> 4;
                    cc->arp_y = data & 0x0F;
                }
                break;
            case 0xB:  // position jump
                *jump_order = data;
                break;
            case 0xC:  // set volume
                cc->volume = data > 64 ? 64 : data;
                break;
            case 0xF:  // set speed (<32) / tempo (>=32)
                if (data > 0) {
                    if (data < 32) *speed = data;
                    else *tempo = data;
                }
                break;
            default:
                break;
        }
    }
}

// Mix one tick of audio. `tick` is the tick index within the current row and
// selects the arpeggio step (base, +x, +y cycling).
static int render_tick(const ModFile* mf, ModChannel* ch, int n, int tick,
                       int sample_rate, Buf* out) {
    if (!buf_reserve(out, (size_t)n)) return 0;
    int arp_step = (tick % 3 == 1) ? 1 : (tick % 3 == 2) ? 2 : 0;

    for (int i = 0; i < n; i++) {
        int32_t acc = 0;
        for (int c = 0; c < mf->nchannels; c++) {
            ModChannel* cc = &ch[c];
            if (!cc->sample) continue;

            int period = cc->period;
            if ((cc->arp_x || cc->arp_y)) {
                int note = cc->base_note +
                           (arp_step == 1 ? cc->arp_x
                                          : arp_step == 2 ? cc->arp_y : 0);
                if (note > MOD_NOTES - 1) note = MOD_NOTES - 1;
                if (note < 0) note = 0;
                period = k_period[note];
            }
            if (period <= 0) continue;

            double freq = MOD_CLOCK / (period * 2.0);
            double step = freq / (double)sample_rate;

            int idx = (int)cc->pos;
            if (cc->sample->loop_length > 0) {
                if (idx >= cc->sample->loop_start + cc->sample->loop_length) {
                    cc->pos = cc->sample->loop_start;
                    idx = (int)cc->pos;
                }
            } else {
                if (idx >= cc->sample->length) {
                    cc->sample = NULL;  // one-shot finished
                    continue;
                }
            }
            if (idx >= 0 && idx < cc->sample->length) {
                acc += (int32_t)cc->sample->data[idx] * cc->volume;
            }
            cc->pos += step;
        }
        // Sum of 4 channels, each at most 127*64, already fits in int16.
        if (acc > 32767) acc = 32767;
        if (acc < -32768) acc = -32768;
        out->p[out->len++] = (int16_t)acc;
    }
    return 1;
}

int16_t* mod_render_pcm(const void* data, size_t size, int sample_rate,
                        int* out_frames) {
    if (out_frames) *out_frames = 0;
    if (!data || size < 1084 || sample_rate <= 0) return NULL;

    const uint8_t* d = (const uint8_t*)data;
    ModFile mf;
    memset(&mf, 0, sizeof(mf));

    // 31 instrument headers, 30 bytes each, starting at offset 20.
    size_t off = 20;
    for (int i = 0; i < 31; i++) {
        const uint8_t* inst = d + off;
        // Length / loop start / loop length are stored in words (16-bit).
        int length = (((int)inst[22] << 8) | inst[23]) * 2;
        int volume = inst[25];
        int repstart = (((int)inst[26] << 8) | inst[27]) * 2;
        int replen = (((int)inst[28] << 8) | inst[29]) * 2;
        if (volume > 64) volume = 64;
        mf.samples[i].length = length > 0 ? length : 0;
        mf.samples[i].volume = volume;
        mf.samples[i].loop_start = repstart;
        // A loop length of <= 2 bytes (1 word) means "no loop" (one-shot).
        mf.samples[i].loop_length = replen > 2 ? replen : 0;
        off += 30;
    }

    int songlen = d[off];
    if (songlen < 1) songlen = 1;
    if (songlen > 128) songlen = 128;
    mf.song_length = songlen;
    mf.restart = d[off + 1];
    memcpy(mf.order, d + off + 2, 128);
    off += 2 + 128;

    const uint8_t* magic = d + off;
    off += 4;
    int nch = 4;
    if (memcmp(magic, "8CHN", 4) == 0 || memcmp(magic, "CD81", 4) == 0 ||
        memcmp(magic, "OKTA", 4) == 0) {
        nch = 8;
    } else if (memcmp(magic, "6CHN", 4) == 0) {
        nch = 6;
    }
    mf.nchannels = nch;

    int maxpat = 0;
    for (int i = 0; i < songlen; i++) {
        if (mf.order[i] > maxpat) maxpat = mf.order[i];
    }
    mf.npatterns = maxpat + 1;
    mf.patterns = d + off;

    size_t sample_off = off + (size_t)mf.npatterns * 64 * nch * 4;
    for (int i = 0; i < 31; i++) {
        if (mf.samples[i].length > 0 && sample_off + (size_t)mf.samples[i].length <= size) {
            mf.samples[i].data = (const int8_t*)(d + sample_off);
            sample_off += mf.samples[i].length;
        } else {
            mf.samples[i].data = NULL;
            mf.samples[i].length = 0;
            mf.samples[i].loop_length = 0;
        }
    }

    ModChannel ch[MOD_MAX_CHANNELS];
    memset(ch, 0, sizeof(ch));
    for (int i = 0; i < MOD_MAX_CHANNELS; i++) ch[i].base_note = 0;

    int speed = 6, tempo = 125;
    int order_pos = 0, row = 0, tick = 0, jump = -1;
    unsigned char visited[128];
    memset(visited, 0, sizeof(visited));

    Buf out = {0, 0, 0};
    double spt = (double)sample_rate * 2.5 / (double)tempo;  // samples per tick
    int guard = 0;

    while (guard++ < 1000000) {
        if (order_pos >= mf.song_length) {
            order_pos = (mf.restart >= 0 && mf.restart < mf.song_length)
                            ? mf.restart : 0;
        }
        // The visited check only applies when starting a new order position;
        // a row spans several ticks, and row 0 is entered once per order.
        if (row == 0 && tick == 0) {
            if (visited[order_pos]) break;  // reached the loop point
            visited[order_pos] = 1;
        }

        if (tick == 0) {
            process_row(&mf, ch, mf.order[order_pos], row, &speed, &tempo, &jump);
            spt = (double)sample_rate * 2.5 / (double)tempo;
        }
        int n = (int)(spt + 0.5);
        if (n < 1) n = 1;
        if (!render_tick(&mf, ch, n, tick, sample_rate, &out)) {
            free(out.p);
            return NULL;
        }

        tick++;
        if (tick >= speed) {
            tick = 0;
            row++;
            if (row >= 64) {
                row = 0;
                if (jump >= 0) {
                    order_pos = jump;
                    jump = -1;
                } else {
                    order_pos++;
                }
            }
        }
    }

    if (out.len == 0) {
        free(out.p);
        return NULL;
    }
    if (out_frames) *out_frames = (int)out.len;
    return out.p;
}
