from erhe_codegen import *

# Which vertex attributes the LOD simplifier's error metric weighs besides
# the positions (doc/editor/lod.md). Mirrors erhe::primitive::Lod_attributes.
enum("Lod_attribute_set",
    value("positions_only",     0, short_desc="Positions only"),
    value("normals_and_colors", 1, short_desc="Normals and colors"),
    underlying_type=UInt,
)
