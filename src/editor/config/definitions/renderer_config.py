from erhe_codegen import *

struct("Renderer_config",
    version=1,
    short_desc="",
    long_desc="",
    developer=False,
    fields=[
        field(
            "max_material_count",
            Int,
            added_in=1,
            default="1000",
            short_desc="Max Material Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_light_count",
            Int,
            added_in=1,
            default="32",
            short_desc="Max Light Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_camera_count",
            Int,
            added_in=1,
            default="256",
            short_desc="Max Camera Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_joint_count",
            Int,
            added_in=1,
            default="1000",
            short_desc="Max Skinning Joint Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_primitive_count",
            Int,
            added_in=1,
            default="6000",
            short_desc="Max Mesh Primitive Counht",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_draw_count",
            Int,
            added_in=1,
            default="6000",
            short_desc="Max Draw Count",
            long_desc="",
            visible=True,
            developer=False
        ),
    ],
)
