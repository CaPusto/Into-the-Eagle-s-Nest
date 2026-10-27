#include "eaglenest.h"

void entities_init(void) {
    memset(g_enemies, 0, sizeof(g_enemies));
    memset(g_bullets, 0, sizeof(g_bullets));
    memset(g_deaths, 0, sizeof(g_deaths));
}

int entities_find_free_enemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if ((g_enemies[i].flags & 0x80) == 0) {
            return i;
        }
    }
    return -1;
}

int entities_find_free_bullet(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_bullets[i].active == 0) {
            return i;
        }
    }
    return -1;
}

int entities_find_free_death(void) {
    for (int i = 0; i < MAX_DEATH_ANIMS; i++) {
        if (g_deaths[i].frame == 0) {
            return i;
        }
    }
    return -1;
}

void entities_spawn_enemy(int x, int y, u8 type) {
    int slot = entities_find_free_enemy();
    if (slot < 0) return;

    g_enemies[slot].x = x;
    g_enemies[slot].y = y;
    // state: NMECHR sprite index (direction | frame), randomly seeded on spawn
    // like the original's `(2,a6) = rnd & 7`.
    (void)type;
    g_enemies[slot].state = (u8)(random_byte() & 7);
    // flags bit7 = active, bit4 = starting chase axis (matches original $E0|rnd)
    g_enemies[slot].flags = (u8)(0x80 | (random_byte() & 0x10));
    g_enemies[slot].frame = 0;
}

void entities_spawn_bullet(int x, int y, u8 dir, bool player_bullet) {
    int slot = entities_find_free_bullet();
    if (slot < 0) return;

    g_bullets[slot].active = player_bullet ? 0xFF : 0x14;
    g_bullets[slot].x = x;
    g_bullets[slot].y = y;
    g_bullets[slot].dir = dir;
}

void entities_spawn_death(int x, int y) {
    int slot = entities_find_free_death();
    if (slot < 0) return;

    g_deaths[slot].x = x;
    g_deaths[slot].y = y;
    g_deaths[slot].frame = 3;
}

void entities_update_deaths(void) {
    for (int i = 0; i < MAX_DEATH_ANIMS; i++) {
        if (g_deaths[i].frame > 0) {
            g_deaths[i].frame--;
        }
    }
}

bool entities_check_collision(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 &&
            y1 < y2 + h2 && y1 + h1 > y2);
}