from erhe_codegen import *

struct("Camera_controls_config",
    reflect=True,
    version=1,
    short_desc="Camera Control",
    long_desc="",
    developer=False,
    fields=[
        field(
            "invert_x",
            Bool,
            added_in=1,
            default="false",
            short_desc="Invert Mouse X Direction",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "invert_y",
            Bool,
            added_in=1,
            default="false",
            short_desc="Invert Mouse Y Direction",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "sensitivity",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Camera Sensitivity",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "velocity_damp",
            Float,
            added_in=1,
            default="0.92f",
            short_desc="Velocity Damp",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "velocity_max_delta",
            Float,
            added_in=1,
            default="0.004f",
            short_desc="Velocity Max Delta",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "move_power",
            Float,
            added_in=1,
            default="1000.0f",
            short_desc="Move Power",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "move_speed",
            Float,
            added_in=1,
            default="2.0f",
            short_desc="Move Speed",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "turn_speed",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Turn Speed",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "ortho_zoom_mode",
            EnumRef("Ortho_zoom_mode"),
            added_in=1,
            default="Ortho_zoom_mode::size_and_pan",
            short_desc="Orthographic View Zoom",
            long_desc="What zooming (mouse wheel) does in a view with an orthographic camera: Size only changes the size of the view around its center; Size and pan also pans the camera so that the point under the pointer stays under the pointer.",
            visible=True,
            developer=False
        ),
        field(
            "perspective_zoom_mode",
            EnumRef("Perspective_zoom_mode"),
            added_in=1,
            default="Perspective_zoom_mode::hover_point",
            short_desc="Perspective View Zoom",
            long_desc="What zooming (mouse wheel) does in a view with a perspective camera: Along view axis moves the camera along its view axis; Towards hovered point moves the camera towards the point under the pointer, so that point stays under the pointer.",
            visible=True,
            developer=False
        ),
        field(
            "zoom_glide_direction",
            EnumRef("Zoom_glide_direction"),
            added_in=1,
            default="Zoom_glide_direction::follow_view",
            short_desc="Zoom Glide Direction",
            long_desc="What a zoom glide does when the camera turns while the glide runs: Keep world direction keeps the world direction captured at the wheel step; Follow view keeps the direction relative to the view, so it turns with the camera.",
            visible=True,
            developer=False
        ),
    ],
)
