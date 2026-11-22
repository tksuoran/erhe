from erhe_codegen import *

# The Foliage Paint tool's values and palette (doc/editor/level_tools.md "Foliage paint").
struct("Foliage_paint_config",
    version=1,
    short_desc="Foliage Paint",
    long_desc="The Foliage Paint tool's brush, densities, seed and palette.",
    developer=False,
    fields=[
        field("brush_radius",               Float,  added_in=1, default="2.0f",  short_desc="Brush Radius",               long_desc="Meters: the radius of the brush sphere about the surface point under the pointer (0.1 - 50)."),
        field("paint_density",              Float,  added_in=1, default="0.5f",  short_desc="Paint Density",              long_desc="Items per square meter of the brush circle one paint stamp tries to add (0.01 - 100)."),
        field("remove_density",             Float,  added_in=1, default="1.0f",  short_desc="Remove Density",             long_desc="The share of the foliage items in the brush one remove stamp takes (0 - 1); 1 removes all of them."),
        field("align_to_surface",           Bool,   added_in=1, default="false", short_desc="Align To Surface",           long_desc="Painted items stand on the surface normal instead of upright along world +Y."),
        field("remove_only_palette_shapes", Bool,   added_in=1, default="false", short_desc="Remove Only Palette Shapes", long_desc="A remove stroke takes only the kit pieces of the palette entries' shapes."),
        field("seed",                       Int,    added_in=1, default="1",     short_desc="Seed",                       long_desc="The seed of the next stroke's draws; it advances after every stroke."),
        field("palette",                    String, added_in=1, default='""',    short_desc="Palette",                    long_desc="The palette entries as their canonical text (doc/editor/level_tools.md \"Foliage paint\"); empty for none."),
    ],
)
