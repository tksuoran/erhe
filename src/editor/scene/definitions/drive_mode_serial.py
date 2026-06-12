from erhe_codegen import *

enum("Drive_mode_serial",
    value("e_force",        0, short_desc="Drive force limited by max_force"),
    value("e_acceleration", 1, short_desc="Drive acceleration limited by max_force (approximated as force)"),
)
