#ifndef EMBEDDED_ASSETS_H
#define EMBEDDED_ASSETS_H

#include <stddef.h>

typedef struct {
    const char* name;
    const unsigned char* data;
    size_t size;
} EmbeddedAsset;

const EmbeddedAsset* embedded_asset_find(const char* name);
const EmbeddedAsset* embedded_asset_find_by_suffix(const char* suffix);
const EmbeddedAsset* embedded_assets_list(size_t* count);

#endif
