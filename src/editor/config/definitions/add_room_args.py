from erhe_codegen import *

struct("Add_room_args",
    version=1,
    short_desc="Args for scene.add_room",
    long_desc="Per-invocation argument block consumed by scene.add_room. Drives floor-brush construction and the floor-instance insert.",
    developer=False,
    fields=[
        field(
            "floor",
            Bool,
            added_in=1,
            default="true",
            short_desc="Add Floor",
            long_desc="If false, scene.add_room is a no-op. (Equivalent to omitting it from commands.json.)",
            visible=True,
            developer=False
        ),
        field(
            "floor_size",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Floor Size",
            long_desc="Floor extents (used for both x and z); the floor mesh is a flat box of this width and depth.",
            visible=True,
            developer=False
        ),
        field(
            "floor_height",
            Float,
            added_in=1,
            default="0.001f",
            short_desc="Floor Height",
            long_desc="Floor box thickness in y. The collision box uses max(floor_height, 0.10) so very thin visual floors still get a usable collider.",
            visible=True,
            developer=False
        ),
    ],
)
