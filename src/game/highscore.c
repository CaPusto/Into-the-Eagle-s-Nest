#include "eaglenest.h"

#include <stdio.h>
#include <string.h>

// Table of nine heroes. The original keeps eight records at HSCTBL plus the
// "working" entry (lbB0016A0 + HISCORE) that the newly qualified player fills
// in; when they are sorted the top nine are kept. Names are stored as font
// glyph indices, matching the original CHARTBL codes.
HighScore g_highscores[HS_ENTRIES];

// Default names: every record starts out as "PANDORA " (the original's default
// at HSCTBL). Scores are the original's default HISCORE ladder converted to the
// port's decimal format.
static const char kDefaultName[HS_NAME_LEN + 1] = "PANDORA ";
static const u32 kDefaultScores[HS_ENTRIES] = {
    100000u, 90000u, 80000u, 70000u, 60000u,
    50000u, 40000u, 30000u, 1000u
};

// Persisted as "<magic><name[8]><score LE32> * 9". The original kept the table
// only in memory; saving it is a deliberate port deviation requested by the
// user so results survive between runs.
static const char kFileMagic[8] = { 'E', 'A', 'G', 'L', 'E', 'H', 'S', '1' };
#define HS_PATH "highscores.dat"

static void highscores_set_defaults(void)
{
    u8 name[HS_NAME_LEN];
    for (int i = 0; i < HS_NAME_LEN; i++)
        name[i] = renderer_char_to_glyph(kDefaultName[i]);
    for (int i = 0; i < HS_ENTRIES; i++) {
        memcpy(g_highscores[i].name, name, HS_NAME_LEN);
        g_highscores[i].score = kDefaultScores[i];
    }
}

void highscores_init(void)
{
    highscores_set_defaults();
    highscores_load();
}

bool highscores_load(void)
{
    FILE* f = fopen(HS_PATH, "rb");
    if (!f)
        return false;

    char magic[8];
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, kFileMagic, sizeof(magic)) != 0) {
        fclose(f);
        return false;
    }

    HighScore loaded[HS_ENTRIES];
    for (int i = 0; i < HS_ENTRIES; i++) {
        u8 raw[4];
        if (fread(loaded[i].name, 1, HS_NAME_LEN, f) != HS_NAME_LEN ||
            fread(raw, 1, sizeof(raw), f) != sizeof(raw)) {
            fclose(f);
            return false;
        }
        loaded[i].score = (u32)raw[0] | ((u32)raw[1] << 8) |
                          ((u32)raw[2] << 16) | ((u32)raw[3] << 24);
    }
    fclose(f);

    memcpy(g_highscores, loaded, sizeof(g_highscores));
    return true;
}

void highscores_save(void)
{
    FILE* f = fopen(HS_PATH, "wb");
    if (!f)
        return;

    fwrite(kFileMagic, 1, sizeof(kFileMagic), f);
    for (int i = 0; i < HS_ENTRIES; i++) {
        u8 raw[4];
        raw[0] = (u8)(g_highscores[i].score & 0xFF);
        raw[1] = (u8)((g_highscores[i].score >> 8) & 0xFF);
        raw[2] = (u8)((g_highscores[i].score >> 16) & 0xFF);
        raw[3] = (u8)((g_highscores[i].score >> 24) & 0xFF);
        fwrite(g_highscores[i].name, 1, HS_NAME_LEN, f);
        fwrite(raw, 1, sizeof(raw), f);
    }
    fclose(f);
}

u32 highscores_last_score(void)
{
    return g_highscores[HS_ENTRIES - 1].score;
}

void highscores_insert(const u8 name[HS_NAME_LEN], u32 score)
{
    // The original bubbles the new record up while it compares greater than or
    // equal to the entry above it (CMPSCR). Matching that means a tie keeps the
    // newcomer below an existing equal score.
    int pos = HS_ENTRIES;
    for (int i = 0; i < HS_ENTRIES; i++) {
        if (score >= g_highscores[i].score) {
            pos = i;
            break;
        }
    }
    if (pos >= HS_ENTRIES)
        return;

    for (int i = HS_ENTRIES - 1; i > pos; i--)
        g_highscores[i] = g_highscores[i - 1];

    memcpy(g_highscores[pos].name, name, HS_NAME_LEN);
    g_highscores[pos].score = score;

    highscores_save();
}
