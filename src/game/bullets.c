#include "eaglenest.h"

// Direction codes: 0=down, 1=up, 2=left, 3=right.
static const int g_bullet_dx[4] = {0, 0, -4, 4};
static const int g_bullet_dy[4] = {4, -4, 0, 0};

void bullets_init(void) {
    memset(g_bullets, 0, sizeof(g_bullets));
}

// Handle a bullet hitting a map tile, mirroring the original DOBUL10 dispatch.
// Returns true if the tile consumes the bullet (the bullet is destroyed).
static bool bullet_hit_tile(int tx, int ty) {
    u8 v = map_get_tile(tx, ty);
    if (v == 0) return false;

    // Wooden doors (0x74 horizontal, 0x75 vertical) are blown open.
    if (v == 0x74 || v == 0x75) {
        map_break_wood_door(tx, ty);
        return true;
    }

    // Detonator wall (0x5F top, 0x5E bottom): SHTDET clears the pair and arms
    // this floor's charge. On non-blow-up missions the wall is inert and just
    // stops the bullet.
    if (v == 0x5E || v == 0x5F) {
        if (map_detonator_hit(tx, ty)) objects_show_message("DETONATOR ACTIVATED");
        return true;
    }

    // 2-tall chests/barrels (0x70/0x71 closed, 0x72/0x73 opened). Shooting one
    // changes the pair and awards 25 points (SHTOB0..3).
    if (v >= 0x70 && v <= 0x73) {
        switch (v) {
            case 0x70: map_set_tile(tx, ty, 0x72); map_set_tile(tx, ty + 1, 0x73); break;
            case 0x71: map_set_tile(tx, ty - 1, 0x72); map_set_tile(tx, ty, 0x73); break;
            case 0x72: map_set_tile(tx, ty, 0); map_set_tile(tx, ty + 1, 0); break;
            case 0x73: map_set_tile(tx, ty - 1, 0); map_set_tile(tx, ty, 0); break;
        }
        player_add_score(25);
        return true;
    }

    // Dynamite (0x76/0x77): the shot detonates it. In the original SHTDYN this
    // is a lethal explosion -- the player dies unless cheating.
    if (v == 0x76 || v == 0x77) {
        int top = (v == 0x77) ? ty - 1 : ty;
        map_set_tile(tx, top, 0);
        map_set_tile(tx, top + 1, 0);
        game_trigger_explosion();
        return true;
    }

    // Treasure panels (0x78 top / 0x79 bottom): shooting reveals a jewel pair,
    // randomly a plain jewel, a collectible one, or a dynamite trap (CHKJWL).
    if (v == 0x78 || v == 0x79) {
        int top = (v == 0x79) ? ty - 1 : ty;
        u8 t0 = 0x7A, t1 = 0x7B;
        if ((random_byte() & 3) == 0) {
            if (random_byte() & 1) { t0 = 0x76; t1 = 0x77; }  // 1/8 dynamite
            else                   { t0 = 0x7C; t1 = 0x7D; }  // 1/8 collectible
        }
        map_set_tile(tx, top, t0);
        map_set_tile(tx, top + 1, t1);
        return true;
    }
    // Collectible jewel (0x7C/0x7D): a shot reveals the plain jewel pair.
    if (v == 0x7C || v == 0x7D) {
        int top = (v == 0x7D) ? ty - 1 : ty;
        map_set_tile(tx, top, 0x7A);
        map_set_tile(tx, top + 1, 0x7B);
        return true;
    }
    // Plain jewel (0x7A/0x7B): bullet stops, nothing changes.
    if (v == 0x7A || v == 0x7B) return true;

    // Searchable objects (0x60..0x6F) placed by the object table: shooting one
    // opens it and reveals/collects its contents (as if walked into).
    if (v >= 0x60 && v <= 0x6F) {
        // 0x61 is the right half of the ammo box and the table in a seated
        // officer figure: in the original it just stops the bullet (DOBUL1).
        if (v == 0x61) return true;
        objects_open_at(tx, ty);
        return true;
    }

    // "Drunken officers" (SHTCMD): static officer figures painted as tiles
    // 0x46..0x57. Shooting one collapses the figure to a corpse and reports it.
    if (v >= 0x46 && v <= 0x57) {
        if (map_shot_officer(tx, ty, v)) {
            objects_show_message("AN OFFICER");
        }
        return true;
    }

    // Everything else: solid wall/door stops the bullet, floor lets it pass.
    return map_is_solid(tx, ty);
}

void bullets_update(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet* b = &g_bullets[i];
        if (b->active == 0) continue;

        if (b->active == 0x14) {
            b->active = 0xFF;
            continue;
        }

        int dx = g_bullet_dx[b->dir];
        int dy = g_bullet_dy[b->dir];

        b->x += dx;
        b->y += dy;

        int tile_x = b->x / TILE_W;
        int tile_y = b->y / TILE_H;

        if (b->x < 0 || b->x >= MAP_BUFFER_W * TILE_W ||
            b->y < 0 || b->y >= MAP_BUFFER_H * TILE_H) {
            b->active = 0;
            continue;
        }

        // The prisoner: a direct hit kills him, a near miss wakes him. A direct
        // hit consumes the bullet.
        if (b->active == 0xFF && prisoner_bullet(b->x, b->y)) {
            b->active = 0;
            continue;
        }

        if (bullet_hit_tile(tile_x, tile_y)) {
            effects_spawn_sparks(b->x, b->y);
            audio_play_bang();
            b->active = 0;
            continue;
        }

        // A bullet stops in the enemy it kills -- it must not pass through and
        // take out a second one lined up behind.
        if (b->active == 0xFF && enemies_damage_at(b->x, b->y)) {
            b->active = 0;
        }
    }
}

void bullets_draw(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet* b = &g_bullets[i];
        if (b->active == 0) continue;

        renderer_draw_rect(b->x - g_scroll_x - 1, b->y - g_scroll_y - 1, 3, 3, 10);
    }
}