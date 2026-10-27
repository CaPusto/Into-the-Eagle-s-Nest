#include "eaglenest.h"

// Small impact sparks. Sparks radiate a short distance from the point of
// impact and fade out. (The muzzle flash is not drawn here: it is part of the
// player's dedicated firing sprite, as in the original.)

typedef struct {
    int x, y;
    int vx, vy;
    u8 life;
    u8 color;
} Spark;

static Spark g_sparks[MAX_SPARKS];

void effects_init(void) {
    memset(g_sparks, 0, sizeof(g_sparks));
}

void effects_spawn_sparks(int x, int y) {
    static const u8 cols[4] = {11, 4, 3, 15};
    for (int n = 0; n < 10; n++) {
        int slot = -1;
        for (int i = 0; i < MAX_SPARKS; i++) {
            if (g_sparks[i].life == 0) { slot = i; break; }
        }
        if (slot < 0) return;

        int vx = 1 + (random_byte() % 4);   // 1..4
        int vy = 1 + (random_byte() % 4);
        if (random_byte() & 1) vx = -vx;
        if (random_byte() & 1) vy = -vy;

        g_sparks[slot].x = x;
        g_sparks[slot].y = y;
        g_sparks[slot].vx = vx;
        g_sparks[slot].vy = vy;
        g_sparks[slot].life = 5 + (random_byte() % 8);   // 5..12 frames
        g_sparks[slot].color = cols[random_byte() & 3];
    }
}

void effects_update(void) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark* s = &g_sparks[i];
        if (s->life == 0) continue;
        s->x += s->vx;
        s->y += s->vy;
        // Decay velocity by 1 per frame so sparks fly a short distance and stop.
        if (s->vx > 0) s->vx--;
        else if (s->vx < 0) s->vx++;
        if (s->vy > 0) s->vy--;
        else if (s->vy < 0) s->vy++;
        s->life--;
    }
}

void effects_draw(void) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark* s = &g_sparks[i];
        if (s->life == 0) continue;
        u8 col = (s->life <= 2) ? 14 : s->color;   // fade to dark red at the end
        renderer_draw_rect(s->x - g_scroll_x, s->y - g_scroll_y, 2, 2, col);
    }
}
