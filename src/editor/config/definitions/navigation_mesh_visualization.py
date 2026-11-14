from erhe_codegen import *

# Which polygons of the scene's navigation mesh a view draws
# (doc/editor/navigation.md "Visualization").
enum("Navigation_mesh_visualization",
    value("off",       0, short_desc="Off"),
    value("reachable", 1, short_desc="Reachable"),
    value("all",       2, short_desc="All"),
    underlying_type=UInt,
)
