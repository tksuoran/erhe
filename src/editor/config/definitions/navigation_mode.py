from erhe_codegen import *

# What Export does about the navigation mesh (doc/editor/navigation.md "Settings").
enum("Navigation_mode",
    value("off",   0, short_desc="No navigation mesh"),
    value("build", 1, short_desc="Build the navigation mesh at the start of the Export"),
    underlying_type=UInt,
)
