from erhe_codegen import *

# Serialization uses value names (strings), so renumbering "all" when
# "hovered" was inserted does not break previously saved config files.
enum("Visualization_mode",
    value("off",      0, short_desc="Off"),
    value("selected", 1, short_desc="Selected"),
    value("hovered",  2, short_desc="Hovered"),
    value("all",      3, short_desc="All"),
    underlying_type=UInt,
)
