from erhe_codegen import *

struct("Make_mesh_args",
    version=1,
    short_desc="Args for scene.add_* mesh commands",
    long_desc="Per-invocation argument block consumed by scene.add_platonic_solids, scene.add_johnson_solids, scene.add_curved_shapes, scene.add_chain, scene.add_toruses. Mirrors the runtime Make_mesh_config minus the material field (which is null for script-driven invocations).",
    developer=False,
    fields=[
        field(
            "instance_count",
            Int,
            added_in=1,
            default="1",
            short_desc="Instance count",
            long_desc="Number of instances each scene.add_* mesh command will create.",
            visible=True,
            developer=False
        ),
        field(
            "instance_gap",
            Float,
            added_in=1,
            default="0.5f",
            short_desc="Instance gap",
            long_desc="Spacing added between adjacent instances during 2D packing.",
            visible=True,
            developer=False
        ),
        field(
            "object_scale",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Object scale",
            long_desc="Uniform scale applied to each instance.",
            visible=True,
            developer=False
        ),
        field(
            "detail",
            Int,
            added_in=1,
            default="4",
            short_desc="Detail",
            long_desc="Subdivision level for brushes that support detail levels. First mesh-command invocation determines the detail used for brush construction; subsequent invocations reuse the same brushes.",
            visible=True,
            developer=False
        ),
        field(
            "mass_scale",
            Float,
            added_in=1,
            default="0.0f",
            short_desc="Mass Scale",
            long_desc="Density used when constructing brush rigid-body shapes. First mesh-command invocation determines the mass_scale used for brush construction; subsequent invocations reuse the same brushes.",
            visible=True,
            developer=False
        ),
    ],
)
