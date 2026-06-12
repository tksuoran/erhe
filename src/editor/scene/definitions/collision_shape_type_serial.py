from erhe_codegen import *

enum("Collision_shape_type_serial",
    value("e_not_specified",  0, short_desc="Shape not stored; derive convex hull from mesh (legacy)"),
    value("e_empty",          1, short_desc="No collision"),
    value("e_box",            2),
    value("e_sphere",         3),
    value("e_capsule",        4),
    value("e_cylinder",       5),
    value("e_compound",       6),
    value("e_tapered_capsule", 7),
    value("e_tapered_cylinder", 8),
    value("e_convex_hull",    9, short_desc="Convex hull rebuilt from source node mesh geometry at load"),
    value("e_mesh",           10, short_desc="Triangle mesh rebuilt from source node mesh geometry at load"),
)
