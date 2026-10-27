#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define TILE_W 16
#define TILE_H 8
#define TILE_BYTES 64

typedef struct {
    uint8_t data[TILE_BYTES];
} Tile;

static void amiga_to_tile(const uint8_t* src, Tile* dst) {
    // Amiga: 64 bytes = 4 planes × 8 rows × 2 bytes (16 pixels per row)
    // src format: row-interleaved: row0: p0_word, p1_word, p2_word, p3_word
    // dst format: plane-interleaved: plane0[16 bytes], plane1[16], plane2[16], plane3[16]
    // Each plane: 8 rows × 2 bytes = 16 bytes: [high byte (left 8, MSB-first), low byte]
    // Plane order/significance: p0=bit0 ... p3=bit3 (Amiga standard)
    // Bit order: MSB of word = bit 15 = leftmost pixel; high byte = pixels x0..x7
    for (int y = 0; y < 8; y++) {
        for (int p = 0; p < 4; p++) {
            int word_offset = y * 8 + p * 2;
            uint16_t word = (src[word_offset] << 8) | src[word_offset + 1];
            int dst_offset = p * 16 + y * 2;
            dst->data[dst_offset] = (uint8_t)(word >> 8);     // HIGH byte (left 8 px)
            dst->data[dst_offset + 1] = (uint8_t)(word & 0xFF); // LOW byte (right 8 px)
        }
    }
}

static void convert_tileset(const char* input, const char* output) {
    FILE* in = fopen(input, "rb");
    if (!in) {
        fprintf(stderr, "Cannot open %s\n", input);
        return;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    printf("DEBUG tileset: size=%ld\n", size);
    fflush(stdout);
    int num_tiles = size / 64;
    if (num_tiles > 256) num_tiles = 256;
    printf("DEBUG: allocating buffer of size %ld\n", size);
    fflush(stdout);
    uint8_t* buffer = malloc(size);
    printf("DEBUG: buffer = %p\n", (void*)buffer);
    fflush(stdout);
    fread(buffer, 1, size, in);
    fclose(in);

    printf("DEBUG: allocating tiles array\n");
    fflush(stdout);
    Tile* tiles = calloc(256, sizeof(Tile));
    printf("DEBUG: tiles = %p\n", (void*)tiles);
    fflush(stdout);

    for (int i = 0; i < num_tiles && i < 256; i++) {
        printf("DEBUG: processing tile %d\n", i);
        fflush(stdout);
        amiga_to_tile(buffer + i * 64, &tiles[i]);
    }
    printf("DEBUG: tile loop done\n");
    fflush(stdout);

    FILE* out = fopen(output, "wb");
    printf("DEBUG: writing tileset to %s\n", output);
    fflush(stdout);
    fwrite(tiles, sizeof(Tile), 256, out);
    fclose(out);

    free(buffer);
    free(tiles);
    printf("Converted %d tiles to %s\n", num_tiles, output);
}

static void convert_font(const char* input, const char* output) {
    FILE* in = fopen(input, "rb");
    if (!in) {
        fprintf(stderr, "Cannot open %s\n", input);
        return;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    printf("DEBUG font: size=%ld\n", size);
    int num_chars = size / 64;
    if (num_chars > 46) num_chars = 46;
    uint8_t* buffer = malloc(size);
    fread(buffer, 1, size, in);
    fclose(in);

    Tile* tiles = calloc(46, sizeof(Tile));

    for (int i = 0; i < num_chars && i < 46; i++) {
        amiga_to_tile(buffer + i * 64, &tiles[i]);
    }

    FILE* out = fopen(output, "wb");
    fwrite(tiles, sizeof(Tile), 46, out);
    fclose(out);

    free(buffer);
    free(tiles);
    printf("Converted %d font chars to %s\n", num_chars, output);
}

static void convert_blocks(const char* input, const char* output) {
    FILE* in = fopen(input, "rb");
    if (!in) {
        fprintf(stderr, "Cannot open %s\n", input);
        return;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    uint8_t* buffer = malloc(size);
    fread(buffer, 1, size, in);
    fclose(in);

    FILE* out = fopen(output, "wb");
    fwrite(buffer, 1, size, out);
    fclose(out);

    free(buffer);
    printf("Copied %ld bytes blocks to %s\n", size, output);
}

static void convert_map(const char* input, const char* output, int level) {
    FILE* in = fopen(input, "rb");
    if (!in) {
        fprintf(stderr, "Cannot open %s\n", input);
        return;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    uint8_t* buffer = malloc(size);
    fread(buffer, 1, size, in);
    fclose(in);

    FILE* out = fopen(output, "wb");
    fwrite(buffer + level * 1024, 1, 1024, out);
    fclose(out);

    free(buffer);
    printf("Extracted map %d (%d bytes) to %s\n", level, 1024, output);
}

// Convert a full-screen Amiga planar image (320x200, 4 planes).
// Input may have a 32-byte palette header (16 Amiga 12-bit colors).
// Output: <stem>.scr = 32000 planar bytes, <stem>.pal = 32 palette bytes.
static void convert_screen(const char* input, const char* stem) {
    FILE* in = fopen(input, "rb");
    if (!in) {
        fprintf(stderr, "Cannot open %s\n", input);
        return;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    uint8_t* buffer = malloc(size);
    fread(buffer, 1, size, in);
    fclose(in);

    int off = 0;
    if (size == 32032) off = 32;  // palette header present

    char scr[256], pal[256];
    snprintf(scr, sizeof(scr), "%s.scr", stem);
    snprintf(pal, sizeof(pal), "%s.pal", stem);

    FILE* out = fopen(scr, "wb");
    fwrite(buffer + off, 1, 32000, out);
    fclose(out);

    if (off == 32) {
        out = fopen(pal, "wb");
        fwrite(buffer, 1, 32, out);
        fclose(out);
    } else {
        remove(pal);
    }

    free(buffer);
    printf("Converted screen (%ld bytes) to %s\n", size, scr);
}

static void convert_raw_audio(const char* input, const char* output) {
    FILE* in = fopen(input, "rb");
    if (!in) {
        fprintf(stderr, "Cannot open %s\n", input);
        return;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    uint8_t* buffer = malloc(size);
    fread(buffer, 1, size, in);
    fclose(in);

    int16_t* samples = malloc(size * 2);
    for (long i = 0; i < size; i++) {
        samples[i] = (int16_t)((buffer[i] - 128) << 8);
    }

    FILE* out = fopen(output, "wb");
    fwrite(samples, sizeof(int16_t), size, out);
    fclose(out);

    free(buffer);
    free(samples);
    printf("Converted %ld bytes audio to %s\n", size, output);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <source_dir> <dest_dir>\n", argv[0]);
        return 1;
    }

    char src[256], dst[256];

    printf("Starting conversion...\n");
    fflush(stdout);
    snprintf(src, sizeof(src), "%s/gfx", argv[1]);
    snprintf(dst, sizeof(dst), "%s/tileset.bin", argv[2]);
    printf("Converting tileset...\n");
    fflush(stdout);
    convert_tileset(src, dst);
    printf("Tileset done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/font", argv[1]);
    snprintf(dst, sizeof(dst), "%s/font.bin", argv[2]);
    printf("Converting font...\n");
    fflush(stdout);
    convert_font(src, dst);
    printf("Font done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/blocks", argv[1]);
    snprintf(dst, sizeof(dst), "%s/blocks.bin", argv[2]);
    printf("Converting blocks...\n");
    fflush(stdout);
    convert_blocks(src, dst);
    printf("Blocks done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/maps", argv[1]);
    printf("Converting maps from maps...\n");
    fflush(stdout);
    for (int i = 0; i < 4; i++) {
        snprintf(dst, sizeof(dst), "%s/map%d.bin", argv[2], i);
        convert_map(src, dst, i);
    }
    printf("Maps from maps done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/maps2", argv[1]);
    printf("Converting maps from maps2...\n");
    fflush(stdout);
    for (int i = 0; i < 4; i++) {
        snprintf(dst, sizeof(dst), "%s/map%d.bin", argv[2], 4 + i);
        convert_map(src, dst, i);
    }
    printf("Maps from maps2 done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/ms.raw", argv[1]);
    snprintf(dst, sizeof(dst), "%s/ms", argv[2]);
    printf("Converting mindscape screen...\n");
    fflush(stdout);
    convert_screen(src, dst);
    printf("Mindscape screen done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/scrn.raw", argv[1]);
    snprintf(dst, sizeof(dst), "%s/scrn", argv[2]);
    printf("Converting title screen...\n");
    fflush(stdout);
    convert_screen(src, dst);
    printf("Title screen done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/panel.raw", argv[1]);
    snprintf(dst, sizeof(dst), "%s/panel", argv[2]);
    printf("Converting panel...\n");
    fflush(stdout);
    convert_screen(src, dst);
    printf("Panel done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/gun2.dig", argv[1]);
    snprintf(dst, sizeof(dst), "%s/gun.raw", argv[2]);
    printf("Converting gun...\n");
    fflush(stdout);
    convert_raw_audio(src, dst);
    printf("Gun done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/clank.dig", argv[1]);
    snprintf(dst, sizeof(dst), "%s/clank.raw", argv[2]);
    printf("Converting clank...\n");
    fflush(stdout);
    convert_raw_audio(src, dst);
    printf("Clank done\n");
    fflush(stdout);

    snprintf(src, sizeof(src), "%s/bang.dig", argv[1]);
    snprintf(dst, sizeof(dst), "%s/bang.raw", argv[2]);
    printf("Converting bang...\n");
    fflush(stdout);
    convert_raw_audio(src, dst);
    printf("Bang done\n");
    fflush(stdout);

    printf("Asset conversion complete!\n");
    return 0;
}