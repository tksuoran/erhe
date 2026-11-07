from erhe_codegen import *

# How Export writes the raw block images it transcodes for a target
# (doc/editor/prepared_assets.md "Export settings").
enum("Texture_supercompression",
    value("zstd", 0, short_desc="Zstandard supercompressed"),
    value("none", 1, short_desc="None (stored as they upload)"),
    underlying_type=UInt,
)
