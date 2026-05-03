from erhe_codegen import *

struct("Commands_config",
    version=1,
    short_desc="Editor startup script",
    long_desc="List of erhe commands executed once at editor startup, plus a Make_mesh_config block consumed by the scene.add_* commands.",
    developer=False,
    fields=[
        field(
            "instance_count",
            Int,
            added_in=1,
            default="1",
            short_desc="Instance count",
            long_desc="Make_mesh_config.instance_count for scene.add_* commands.",
            visible=True,
            developer=False
        ),
        field(
            "instance_gap",
            Float,
            added_in=1,
            default="0.5f",
            short_desc="Instance gap",
            long_desc="Make_mesh_config.instance_gap for scene.add_* commands.",
            visible=True,
            developer=False
        ),
        field(
            "object_scale",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Object scale",
            long_desc="Make_mesh_config.object_scale for scene.add_* commands.",
            visible=True,
            developer=False
        ),
        field(
            "detail",
            Int,
            added_in=1,
            default="4",
            short_desc="Detail",
            long_desc="Make_mesh_config.detail for scene.add_* commands.",
            visible=True,
            developer=False
        ),
        field(
            "commands",
            Vector(String),
            added_in=1,
            default="",
            short_desc="Startup command list",
            long_desc="Sequence of registered erhe::commands::Command names to invoke once at editor startup.",
            visible=True,
            developer=False
        ),
    ],
)
