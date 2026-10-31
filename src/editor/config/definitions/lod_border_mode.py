from erhe_codegen import *

# Whether border vertices may move during LOD simplification
# (doc/editor/lod.md). Mirrors erhe::primitive::Lod_border.
enum("Lod_border_mode",
    value("free",   0, short_desc="Free"),
    value("locked", 1, short_desc="Locked"),
    underlying_type=UInt,
)
