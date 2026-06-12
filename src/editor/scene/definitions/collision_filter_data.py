from erhe_codegen import *

struct("Collision_filter_data",
    version=1,
    fields=[
        field("id",                       UInt64,         added_in=1, default="0",  short_desc="File-local id (1-based), referenced by Node_physics_data::filter_id"),
        field("name",                     String,         added_in=1, default='""'),
        field("collision_systems",        Vector(String), added_in=1,               short_desc="Systems this filter's body belongs to"),
        field("collide_with_systems",     Vector(String), added_in=1,               short_desc="Non-empty = allowlist semantics"),
        field("not_collide_with_systems", Vector(String), added_in=1,               short_desc="Denylist; used when collide_with_systems is empty"),
    ],
)
