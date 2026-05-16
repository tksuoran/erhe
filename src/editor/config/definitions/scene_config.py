from erhe_codegen import *

struct("Scene_config",
    version=1,
    short_desc="Default Scene Configuration",
    long_desc="Used to configure Default Scene",
    developer=False,
    fields=[
        field(
            "imgui_window_scene_view",
            Bool,
            added_in=1,
            default="true",
            short_desc="ImGui Window Scene Viewport",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "instance_count",
            Int,
            added_in=1,
            default="1",
            short_desc="Object Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "instance_gap",
            Float,
            added_in=1,
            default="0.50f",
            short_desc="Object Instance Gap (m)",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "object_scale",
            Float,
            added_in=1,
            default="0.25f",
            short_desc="Object Scale",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "make_johnson_solid_brushes",
            Bool,
            added_in=1,
            default="true",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "make_platonic_solid_brushes",
            Bool,
            added_in=1,
            default="true",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "make_curved_brushes",
            Bool,
            added_in=1,
            default="true",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
    ],
)
