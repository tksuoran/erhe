#ifndef ERHE_DDGI_TILES_GLSL
#define ERHE_DDGI_TILES_GLSL

// Probe field atlas tiling (doc/editor/ddgi.md "Data layout"), the GLSL twin
// of get_probe_field_tile() in src/editor/renderers/probe_grid.cpp: probe
// (x, y, z) has the tile index x + counts.x * (z + counts.z * y), wrapped
// into rows of tiles_per_row tiles. The irradiance and distance atlases
// place the probe's octahedral tile there, the probe data texture its one
// texel. Shared by the forward pass sampling (erhe_ddgi.glsl) and the
// producers that write the field (ddgi_trace / ddgi_blend / ddgi_relocate,
// rc_reduce).
ivec2 ddgi_probe_tile(ivec3 coords, ivec3 counts, int tiles_per_row)
{
    int tile_index = coords.x + (counts.x * (coords.z + (counts.z * coords.y)));
    return ivec2(tile_index % tiles_per_row, tile_index / tiles_per_row);
}

#endif // ERHE_DDGI_TILES_GLSL
