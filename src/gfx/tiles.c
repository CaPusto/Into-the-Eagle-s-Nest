#include "eaglenest.h"

void tiles_init(void) {
    memset(g_tileset, 0, sizeof(g_tileset));
    memset(g_font, 0, sizeof(g_font));
}

bool tiles_load_tileset(const char* path) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return false;

    size_t read = SDL_ReadIO(io, g_tileset, sizeof(Tile) * 256);
    SDL_CloseIO(io);
    return read == sizeof(Tile) * 256;
}

bool tiles_load_font(const char* path) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return false;

    size_t read = SDL_ReadIO(io, g_font, sizeof(Tile) * 46);
    SDL_CloseIO(io);
    return read == sizeof(Tile) * 46;
}

bool tiles_load_blocks(const char* path) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return false;

    size_t read = SDL_ReadIO(io, g_block_data, sizeof(g_block_data));
    SDL_CloseIO(io);
    return read == sizeof(g_block_data);
}

bool tiles_load_map(int map_index, const char* path) {
    if (map_index < 0 || map_index >= 8) return false;

    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return false;

    size_t read = SDL_ReadIO(io, g_maps[map_index].tiles, MAP_FILE_SIZE);
    SDL_CloseIO(io);
    return read == MAP_FILE_SIZE;
}

bool tiles_load_all_assets(const char* base_path) {
    char path[256];

    snprintf(path, sizeof(path), "%s/tileset.bin", base_path);
    if (!tiles_load_tileset(path)) return false;

    snprintf(path, sizeof(path), "%s/font.bin", base_path);
    if (!tiles_load_font(path)) return false;

    snprintf(path, sizeof(path), "%s/blocks.bin", base_path);
    if (!tiles_load_blocks(path)) return false;

    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "%s/map%d.bin", base_path, i);
        if (!tiles_load_map(i, path)) return false;
    }

    return true;
}

const Tile* tiles_get(u8 index) {
    return &g_tileset[index];
}

const Tile* tiles_get_font(u8 index) {
    if (index < 46) return &g_font[index];
    return &g_font[0];
}

// Get tile index from block (4x4 tiles per block)
u8 tiles_get_block_tile(u8 block_idx, int tile_x, int tile_y) {
    if (block_idx >= 128) return 0;
    if (tile_x < 0 || tile_x >= 4 || tile_y < 0 || tile_y >= 4) return 0;
    // Block data: 16 bytes = 4x4 tile indices, row-major
    int offset = block_idx * 16 + tile_y * 4 + tile_x;
    return g_block_data[offset];
}

void tiles_decode_amiga_tile(const u8* src_planes[4], Tile* dst) {
    for (int p = 0; p < 4; p++) {
        for (int y = 0; y < 8; y++) {
            u8 byte = src_planes[p][y];
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                int byte_idx = y * 8 + (x / 8);
                if (byte & (1 << bit)) {
                    dst->data[byte_idx + p * 8] |= (1 << bit);
                }
            }
        }
    }
}