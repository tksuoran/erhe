from erhe_codegen import *

struct("Physics_material_data",
    version=1,
    fields=[
        field("id",                  UInt64,                         added_in=1, default="0",    short_desc="File-local id (1-based), referenced by Node_physics_data::material_id"),
        field("name",                String,                         added_in=1, default='""'),
        field("static_friction",     Float,                          added_in=1, default="0.6f"),
        field("dynamic_friction",    Float,                          added_in=1, default="0.6f"),
        field("restitution",         Float,                          added_in=1, default="0.0f"),
        field("friction_combine",    EnumRef("Combine_mode_serial"), added_in=1, default="Combine_mode_serial::e_average"),
        field("restitution_combine", EnumRef("Combine_mode_serial"), added_in=1, default="Combine_mode_serial::e_average"),
    ],
)
