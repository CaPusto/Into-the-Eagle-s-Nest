#!/usr/bin/env python3
import os
import sys

def sanitize(name):
    out = []
    for c in name:
        if c.isalnum():
            out.append(c)
        else:
            out.append('_')
    return ''.join(out)

def main():
    if len(sys.argv) != 3:
        print('usage: embed_assets.py <assets_dir> <out_dir>', file=sys.stderr)
        sys.exit(1)
    assets_dir = sys.argv[1]
    out_dir = sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    entries = []
    for root, dirs, files in os.walk(assets_dir):
        dirs.sort()
        files.sort()
        for fn in files:
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, assets_dir).replace('\\', '/')
            entries.append((rel, full))
    entries.sort()

    h_path = os.path.join(out_dir, 'embedded_assets.h')
    c_path = os.path.join(out_dir, 'embedded_assets.c')

    with open(h_path, 'w') as h:
        h.write('#ifndef EMBEDDED_ASSETS_H\n#define EMBEDDED_ASSETS_H\n\n')
        h.write('#include <stddef.h>\n\n')
        h.write('typedef struct {\n')
        h.write('    const char* name;\n')
        h.write('    const unsigned char* data;\n')
        h.write('    size_t size;\n')
        h.write('} EmbeddedAsset;\n\n')
        h.write('const EmbeddedAsset* embedded_asset_find(const char* name);\n')
        h.write('const EmbeddedAsset* embedded_asset_find_by_suffix(const char* suffix);\n')
        h.write('const EmbeddedAsset* embedded_assets_list(size_t* count);\n\n')
        h.write('#endif\n')

    with open(c_path, 'w') as c:
        c.write('#include "embedded_assets.h"\n')
        c.write('#include <string.h>\n\n')

        for rel, full in entries:
            ident = 'ea_' + sanitize(rel)
            with open(full, 'rb') as f:
                data = f.read()
            c.write(f'const unsigned char {ident}[] = {{\n')
            for i in range(0, len(data), 12):
                chunk = data[i:i+12]
                c.write('    ')
                c.write(', '.join(f'0x{b:02X}' for b in chunk))
                if i + 12 < len(data):
                    c.write(',\n')
                else:
                    c.write('\n')
            c.write('};\n')
            c.write(f'const size_t {ident}_len = {len(data)};\n\n')

        c.write('static const EmbeddedAsset g_embedded_assets[] = {\n')
        for rel, full in entries:
            ident = 'ea_' + sanitize(rel)
            c.write(f'    {{"{rel}", {ident}, {ident}_len}},\n')
        c.write('};\n\n')
        c.write(f'static const size_t g_embedded_assets_count = {len(entries)};\n\n')
        c.write('const EmbeddedAsset* embedded_asset_find(const char* name) {\n')
        c.write('    if (!name) return NULL;\n')
        c.write('    for (size_t i = 0; i < g_embedded_assets_count; i++) {\n')
        c.write('        const char* n = g_embedded_assets[i].name;\n')
        c.write('        /* match exact relative path or basename */\n')
        c.write('        if (strcmp(n, name) == 0) return &g_embedded_assets[i];\n')
        c.write('        const char* bn = strrchr(n, \'/\');\n')
        c.write('        if (bn && strcmp(bn + 1, name) == 0) return &g_embedded_assets[i];\n')
        c.write('    }\n')
        c.write('    return NULL;\n')
        c.write('}\n\n')
        c.write('const EmbeddedAsset* embedded_asset_find_by_suffix(const char* suffix) {\n')
        c.write('    if (!suffix) return NULL;\n')
        c.write('    size_t sl = strlen(suffix);\n')
        c.write('    for (size_t i = 0; i < g_embedded_assets_count; i++) {\n')
        c.write('        const char* n = g_embedded_assets[i].name;\n')
        c.write('        size_t nl = strlen(n);\n')
        c.write('        if (nl >= sl && strcmp(n + nl - sl, suffix) == 0) return &g_embedded_assets[i];\n')
        c.write('    }\n')
        c.write('    return NULL;\n')
        c.write('}\n\n')
        c.write('const EmbeddedAsset* embedded_assets_list(size_t* count) {\n')
        c.write('    if (count) *count = g_embedded_assets_count;\n')
        c.write('    return g_embedded_assets;\n')
        c.write('}\n')

    print(f'Generated {h_path} and {c_path} with {len(entries)} assets')

if __name__ == '__main__':
    main()
