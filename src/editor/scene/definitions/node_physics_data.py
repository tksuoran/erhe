from erhe_codegen import *

struct("Node_physics_data",
    version=2,
    fields=[
        field("node_id",           UInt64,                           added_in=1, default="0",     short_desc="Node this physics body is attached to"),
        field("motion_mode",       EnumRef("Motion_mode_serial"),    added_in=1, default="Motion_mode_serial::e_dynamic"),
        field("friction",          Float,                            added_in=1, default="0.5f"),
        field("restitution",       Float,                            added_in=1, default="0.2f"),
        field("linear_damping",    Float,                            added_in=1, default="0.05f"),
        field("angular_damping",   Float,                            added_in=1, default="0.05f"),
        field("mass",              Optional(Float),                  added_in=1,                   short_desc="Override mass; if absent, derived from density and volume"),
        field("density",           Optional(Float),                  added_in=1,                   short_desc="Density for mass calculation"),
        field("enable_collisions", Bool,                             added_in=1, default="true"),
        field("collision_shape",   StructRef("Collision_shape_data"), added_in=2,                  short_desc="Collision shape type and parameters"),
    ],
)
