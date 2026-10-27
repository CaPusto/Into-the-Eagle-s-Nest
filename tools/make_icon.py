#!/usr/bin/env python3
"""Generate the Windows application icon from the game's hero sprite.

Reads assets/tileset.bin (the Amiga tile charset) and the standard in-game
palette, renders the front-facing "down" walk frame (2x3 tiles = 32x24 px) and
writes a single 32x32 .ico whose image is PNG-compressed (keeps the file tiny).
Colour index 0 is treated as transparent, the same way the renderer treats it
(it skips pen 0).

Usage:
    python3 tools/make_icon.py [tileset.bin] [icon.ico]
"""

import struct
import sys
import zlib

# Standard COLS palette used by palette_init() in src/gfx/palette.c.
PALETTE_RAW = [
    0x000, 0x444, 0x777, 0x330, 0x760, 0x020, 0x040, 0x232,
    0x077, 0x110, 0x743, 0x773, 0x017, 0x730, 0x300, 0x710,
]

# Front-facing idle frame ("down f0" in src/game/player.c).
HERO_FRAME = [0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D]

SPRITE_COLS = 2
SPRITE_ROWS = 3
TILE_SIZE = 16
TILE_HEIGHT = 8

ICON_SIZES = [32]


def palette_rgb():
    out = []
    for c in PALETTE_RAW:
        r = ((c >> 8) & 0xF) * 17
        g = ((c >> 4) & 0xF) * 17
        b = (c & 0xF) * 17
        out.append((r, g, b))
    return out


def decode_tile(data, idx):
    """Return an 8-row x 16-col grid of palette indices for tile `idx`."""
    base = idx * 64
    t = data[base:base + 64]
    grid = [[0] * TILE_SIZE for _ in range(TILE_HEIGHT)]
    for p in range(4):
        for y in range(TILE_HEIGHT):
            for bx in range(2):
                byte = t[p * 16 + y * 2 + bx]
                for bit in range(8):
                    if (byte >> (7 - bit)) & 1:
                        grid[y][bx * 8 + bit] |= 1 << p
    return grid


def render_hero(data, rgb):
    """Render the hero frame into a 32x24 RGBA image (list of rows)."""
    w = SPRITE_COLS * TILE_SIZE
    h = SPRITE_ROWS * TILE_HEIGHT
    img = [[(0, 0, 0, 0)] * w for _ in range(h)]
    for r in range(SPRITE_ROWS):
        for c in range(SPRITE_COLS):
            grid = decode_tile(data, HERO_FRAME[r * SPRITE_COLS + c])
            for y in range(TILE_HEIGHT):
                for x in range(TILE_SIZE):
                    v = grid[y][x]
                    px = (0, 0, 0, 0) if v == 0 else (*rgb[v], 255)
                    img[r * TILE_HEIGHT + y][c * TILE_SIZE + x] = px
    return img


def scale_to_icon(src, size):
    """Nearest-neighbour scale the 32x24 source onto a square RGBA canvas."""
    sw = len(src[0])
    sh = len(src)
    content_h = max(1, round(sh * size / sw))
    y_off = (size - content_h) // 2
    out = [[(0, 0, 0, 0)] * size for _ in range(size)]
    for ty in range(content_h):
        sy = min(sh - 1, ty * sh // content_h)
        for tx in range(size):
            sx = min(sw - 1, tx * sw // size)
            out[y_off + ty][tx] = src[sy][sx]
    return out


def png_bytes(rgba, size):
    """Encode a size x size RGBA image as an 8-bit RGBA PNG."""
    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filter type 0 (None)
        for x in range(size):
            r, g, b, a = rgba[y][x]
            raw += bytes((r, g, b, a))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit, RGBA
    idat = zlib.compress(bytes(raw), 9)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def encode_ico(images):
    """Pack a list of (size, RGBA rows) into an ICO where each image is a PNG."""
    count = len(images)
    header = struct.pack("<HHH", 0, 1, count)
    entries = []
    blobs = []
    offset = 6 + 16 * count
    for size, rgba in images:
        blob = png_bytes(rgba, size)
        dim = 0 if size >= 256 else size
        entries.append(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset))
        offset += len(blob)
        blobs.append(blob)
    return header + b"".join(entries) + b"".join(blobs)


def main():
    tileset = sys.argv[1] if len(sys.argv) > 1 else "assets/tileset.bin"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "src/resources/icon.ico"

    with open(tileset, "rb") as f:
        data = f.read()
    if len(data) < 256 * 64:
        raise SystemExit("tileset too small: %s" % tileset)

    rgb = palette_rgb()
    hero = render_hero(data, rgb)
    images = [(s, scale_to_icon(hero, s)) for s in ICON_SIZES]
    blob = encode_ico(images)

    with open(out_path, "wb") as f:
        f.write(blob)
    print("wrote %s (%d bytes, sizes: %s)" % (out_path, len(blob), ICON_SIZES))


if __name__ == "__main__":
    main()
