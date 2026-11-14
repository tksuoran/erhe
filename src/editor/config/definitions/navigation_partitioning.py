from erhe_codegen import *

# Recast's three region builders (doc/editor/navigation.md "Settings").
enum("Navigation_partitioning",
    value("watershed", 0, short_desc="Watershed (best regions, slowest)"),
    value("monotone",  1, short_desc="Monotone (fastest, long thin regions)"),
    value("layers",    2, short_desc="Layers (non-overlapping layers, for tiles)"),
    underlying_type=UInt,
)
