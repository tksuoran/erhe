from erhe_codegen import *

struct("Joint_limit_data",
    version=1,
    fields=[
        field("linear_axes",  Vector(Int),     added_in=1,                 short_desc="Translation axes (0=X, 1=Y, 2=Z) this limit applies to"),
        field("angular_axes", Vector(Int),     added_in=1,                 short_desc="Rotation axes (0=X, 1=Y, 2=Z) this limit applies to"),
        field("min",          Optional(Float), added_in=1,                 short_desc="Absent = unbounded below"),
        field("max",          Optional(Float), added_in=1,                 short_desc="Absent = unbounded above"),
        field("stiffness",    Optional(Float), added_in=1,                 short_desc="Soft limit spring; absent = hard limit"),
        field("damping",      Float,           added_in=1, default="0.0f", short_desc="Soft limit spring damping"),
    ],
)
