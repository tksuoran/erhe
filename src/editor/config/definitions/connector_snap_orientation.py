from erhe_codegen import *

# How a connector snap turns the moved nodes so the connector frames face each other (U50,
# doc/editor/level_tools.md "Connector snap"); keep and align are the former names of off and full.
enum("Connector_snap_orientation",
    value("off",    0, short_desc="Off: the snap translates only", former_names=["keep"]),
    value("rotate", 1, short_desc="Rotate: with Orient To Surface the snap turns only about the surface normal; without it as Full"),
    value("full",   2, short_desc="Full: the snap turns the frames to face each other, overriding Orient To Surface", former_names=["align"]),
    underlying_type=UInt,
)
