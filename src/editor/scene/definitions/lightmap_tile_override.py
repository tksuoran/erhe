from erhe_codegen import *

# One lightmap quadtree grid leaf override (Scene_settings::
# lightmap_tile_overrides). level != 0: the leaf replaces the level-0 cells
# it covers - subdivided (level > 0, smaller cell, higher texel density) or
# merged (level < 0, larger cell, lower density). Cell size at level L is
# lightmap.cell_size_m * 2^-L; cells are anchored at multiples of their size
# from the world origin (see renderers/lightmap_grid.hpp).
struct("Lightmap_tile_override",
    version=1,
    short_desc="Lightmap quadtree leaf override",
    long_desc="",
    developer=False,
    fields=[
        field("level", Int, added_in=1, default="0", short_desc="Quadtree level (0 = the default cell size; >0 subdivided, <0 merged)"),
        field("ix",    Int, added_in=1, default="0", short_desc="Cell X index at this level"),
        field("iz",    Int, added_in=1, default="0", short_desc="Cell Z index at this level"),
    ],
)
