from erhe_codegen import *

# What a LOD level that misses its target falls back to
# (doc/editor/lod.md). Mirrors erhe::primitive::Lod_fallback.
enum("Lod_fallback_mode",
    value("none",   0, short_desc="None"),
    value("sloppy", 1, short_desc="Sloppy"),
    underlying_type=UInt,
)
