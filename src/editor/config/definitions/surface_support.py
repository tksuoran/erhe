from erhe_codegen import *

# Which base lines of a kit layout element support and block its snap to surface (U52,
# doc/editor/level_tools.md "Surface placement").
enum("Surface_support",
    value("five_lines",      0, short_desc="Five lines: the base center and the four footprint corners find support and refuse a place a face lies inside"),
    value("center_blocks",   1, short_desc="Center blocks: the five lines find support; only a face on the center line refuses a place"),
    value("center_fallback", 2, short_desc="Center fallback: the nearest place; when refused, the surface under the base center along the search direction"),
    underlying_type=UInt,
)
