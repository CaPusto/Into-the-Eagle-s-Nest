#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define TILE_W 8
#define TILE_H 8
#define TILE_BYTES 32
#define PALETTE_SIZE 16

typedef struct {
    uint8_t data[TILE_BYTES];
} Tile;

static const uint32_t amiga_palette[PALETTE_SIZE] = {
    0xFF000000, 0xFF444444, 0xFF777777, 0xFF333300,
    0xFF776600, 0xFF002200, 0xFF004400, 0xFF223322,
    0xFF007777, 0xFF111100, 0xFF774433, 0xFF777733,
    0xFF001177, 0xFF773300, 0xFF330000, 0xFF771100
};

static void write_ppm(const char* filename, int w, int h, uint32_t* pixels) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = pixels[y * w + x];
            uint8_t r = (c >> 16) & 0xFF;
            uint8_t g = (c >> 8) & 0xFF;
            uint8_t b = c & 0xFF;
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        }
    }
    fclose(f);
    printf("Wrote %s\n", filename);
}

static void render_tile(const Tile* tile, uint32_t* out, int stride, int palette_offset) {
    for (int ty = 0; ty < TILE_H; ty++) {
        for (int tx = 0; tx < TILE_W; tx++) {
            int bit = 7 - tx;
            int byte_idx = ty * 8 + (tx / 8);
            uint8_t pixel = (tile->data[byte_idx] >> bit) & 1;
            pixel |= ((tile->data[byte_idx + 8] >> bit) & 1) << 1;
            pixel |= ((tile->data[byte_idx + 16] >> bit) & 1) << 2;
            pixel |= ((tile->data[byte_idx + 24] >> bit) & 1) << 3;
            
            if (pixel != 0) {
                out[ty * stride + tx] = amiga_palette[(pixel + palette_offset) & 0xF];
            } else {
                out[ty * stride + tx] = 0xFF000000;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <tileset.bin> <output_dir>\n", argv[0]);
        return 1;
    }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        printf("Cannot open %s\n", argv[1]);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    int num_tiles = size / TILE_BYTES;
    if (num_tiles > 256) num_tiles = 256;
    
    Tile* tiles = malloc(num_tiles * sizeof(Tile));
    fread(tiles, TILE_BYTES, num_tiles, f);
    fclose(f);
    
    // Export each tile as separate PPM
    for (int i = 0; i < num_tiles; i++) {
        uint32_t pixels[TILE_H * TILE_W];
        render_tile(&tiles[i], pixels, TILE_W, 0);
        
        char outname[256];
        snprintf(outname, sizeof(outname), "%s/tile_%03d.ppm", argv[2], i);
        write_ppm(outname, TILE_W, TILE_H, pixels);
    }
    
    // Export full grid (16x16 tiles)
    int grid_w = 16 * TILE_W;
    int grid_h = 16 * TILE_H;
    uint32_t* grid = malloc(grid_w * grid_h * sizeof(uint32_t));
    memset(grid, 0, grid_w * grid_h * sizeof(uint32_t));
    
    for (int i = 0; i < num_tiles; i++) {
        int gx = (i % 16) * TILE_W;
        int gy = (i / 16) * TILE_H;
        uint32_t* dst = &grid[gy * grid_w + gx];
        render_tile(&tiles[i], dst, grid_w, 0);
    }
    
    write_ppm("tileset_grid.ppm", grid_w, grid_h, grid);
    
    free(grid);
    free(tiles);
    return 0;
}