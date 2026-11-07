from erhe_codegen import *

# What Export does about the potentially visible set (doc/editor/prepared_assets.md
# "PVS", doc/plans/prepared_assets.md R7.6).
enum("Pvs_mode",
    value("off",    0, short_desc="No PVS in the prepared asset"),
    value("reuse",  1, short_desc="Copy the editor asset's PVS item as it is (unsafe: not checked against the exported geometry)"),
    value("rebake", 2, short_desc="Bake a new PVS during every Export"),
    underlying_type=UInt,
)
