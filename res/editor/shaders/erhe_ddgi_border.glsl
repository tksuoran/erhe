#ifndef ERHE_DDGI_BORDER_GLSL
#define ERHE_DDGI_BORDER_GLSL

// Octahedral border of one probe tile of the probe field atlases
// (doc/editor/ddgi.md "Data layout"), shared by the producers that write
// the atlases: ddgi_blend.comp and rc_reduce.comp. The tile is
// interior + 2 texels square; its 1-texel border lets bilinear taps inside
// the tile wrap across the octahedral edges without reaching a neighbouring
// probe.
//
// An edge texel mirrors the interior texel on the opposite side of that
// edge (the octahedral map wraps that way), and a corner texel takes the
// diagonally opposite interior corner.
//
// Reads and writes the storage image i_probe_atlas, which the including
// shader's bind group layout declares. One workgroup fills one tile: every
// invocation calls this with its own first texel and the group size as the
// stride, after the interior texels are written and made visible to the
// workgroup (memoryBarrierImage(); barrier();).
void ddgi_fill_tile_border(ivec2 tile_origin, int interior, int first_texel, int texel_stride)
{
    int tile_size   = interior + 2;
    int texel_count = tile_size * tile_size;
    for (int texel = first_texel; texel < texel_count; texel += texel_stride) {
        ivec2 local = ivec2(texel % tile_size, texel / tile_size);
        bool  x_border = (local.x == 0) || (local.x == tile_size - 1);
        bool  y_border = (local.y == 0) || (local.y == tile_size - 1);
        if (!x_border && !y_border) {
            continue;
        }
        ivec2 source;
        if (x_border && y_border) {
            source = ivec2(
                (local.x == 0) ? interior : 1,
                (local.y == 0) ? interior : 1
            );
        } else if (x_border) {
            source = ivec2((local.x == 0) ? 1 : interior, tile_size - 1 - local.y);
        } else {
            source = ivec2(tile_size - 1 - local.x, (local.y == 0) ? 1 : interior);
        }
        imageStore(i_probe_atlas, tile_origin + local, imageLoad(i_probe_atlas, tile_origin + source));
    }
}

#endif // ERHE_DDGI_BORDER_GLSL
