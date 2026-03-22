from erhe_codegen import *

enum("Collision_shape_type_serial",
    value("e_not_specified",  0, short_desc="Shape not stored; derive convex hull from mesh (legacy)"),
    value("e_empty",          1, short_desc="No collision"),
    value("e_box",            2),
    value("e_sphere",         3),
    value("e_capsule",        4),
    value("e_cylinder",       5),
    value("e_compound",       6),
)
