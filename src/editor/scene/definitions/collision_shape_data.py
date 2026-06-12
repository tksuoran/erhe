from erhe_codegen import *

struct("Collision_shape_data",
    version=3,
    fields=[
        field("type",           EnumRef("Collision_shape_type_serial"),    added_in=1, default="Collision_shape_type_serial::e_not_specified"),
        field("half_extents",   Optional(Vec3),                           added_in=1, short_desc="For box/cylinder"),
        field("radius",         Optional(Float),                          added_in=1, short_desc="For sphere/capsule; bottom radius for tapered capsule/cylinder"),
        field("axis",           Optional(Int),                            added_in=1, short_desc="0=X, 1=Y, 2=Z"),
        field("length",         Optional(Float),                          added_in=1, short_desc="For capsule/tapered capsule/tapered cylinder"),
        field("children",       Vector(StructRef("Compound_child_data")), added_in=1, short_desc="For compound shapes"),
        field("top_radius",     Optional(Float),                          added_in=2, short_desc="For tapered capsule/cylinder"),
        field("source_node_id", Optional(UInt64),                         added_in=3, short_desc="Mesh / convex hull geometry source node; absent = owning node"),
        field("scale",          Optional(Vec3),                           added_in=3, short_desc="Shape scale wrapper to re-apply at load; uniform when all components equal"),
    ],
)
