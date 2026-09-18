# Per-scene texture memory cost

Status: proposed

This plan extends `doc/editor/reloadable_asset_loads.md` (the `get_memory_usage` MCP
tool) with a per-texture breakdown, and records the measurement that motivates
it.

Measured with `get_memory_usage` on a Vulkan Debug build:

```
startup                tex =   380,185,160 (250 textures)
after create_scene     tex = 1,068,779,192 (304 textures)
```

Creating a single EMPTY scene adds about 688 MB across 54 textures. The figure
is an estimate computed from each texture's create info (format x dimensions x
mip levels x layers), so it counts render targets, shadow maps, probe volumes
and lightmap atlases the same as content textures, and it accounts for neither
aliasing nor sparse residency. The cost may be entirely expected, but it is
large enough to break down.

First step: extend `get_memory_usage` to report a per-texture breakdown (debug
label plus bytes) rather than only the total, then diff the list across a
`create_scene` call. The likely contributors are the per-scene shadow map
array, the DDGI probe volume (`ddgi_window.cpp` already reports probe texture
megabytes separately) and the lightmap tile atlases (`resident_tile_budget: 14`
in `config/editor/editor_settings.json`).

If the per-scene cost is real it bounds how many scenes can be open at once far
more tightly than mesh memory does.
