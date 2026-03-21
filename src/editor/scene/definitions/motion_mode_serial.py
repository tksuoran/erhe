from erhe_codegen import *

enum("Motion_mode_serial",
    value("e_static",                 1, short_desc="Immovable"),
    value("e_kinematic_non_physical", 2, short_desc="Movable from scene graph (instant), not from physics"),
    value("e_kinematic_physical",     3, short_desc="Movable from scene graph (creates kinetic energy)"),
    value("e_dynamic",               4, short_desc="Movable from physics simulation"),
)
