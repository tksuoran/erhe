from erhe_codegen import *

# What a Texture item holds for a PNG / JPEG source image
# (doc/editor/prepared_assets.md "Export settings", doc/plans/prepared_assets.md R7.5).
enum("Texture_encoding",
    value("target", 0, short_desc="Encoded to the target's block format by way of its Basis texture item"),
    value("source", 1, short_desc="The source image bytes as they are; the prepared run decodes them at load"),
    underlying_type=UInt,
)
