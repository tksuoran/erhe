from erhe_codegen import *

# What the world-space bake does with a primitive several baked mesh nodes
# share (doc/editor/world_space_bake.md "Export settings").
enum("Bake_instancing",
    value("skip",                    0, short_desc="Skip shared primitives (they keep their node transforms)"),
    value("duplicate",               1, short_desc="Duplicate per instance"),
    value("duplicate_within_budget", 2, short_desc="Duplicate within the memory budget, then skip"),
    underlying_type=UInt,
)
