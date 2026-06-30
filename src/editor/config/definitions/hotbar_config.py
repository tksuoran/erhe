from erhe_codegen import *

struct("Hotbar_config",
    version=4,
    short_desc="Hotbar",
    long_desc="",
    developer=True,
    fields=[
        field(
            "enabled",
            Bool,
            added_in=1,
            default="true",
            short_desc="Enable Hotbar",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "show",
            Bool,
            added_in=1,
            default="true",
            short_desc="Show Hotbar",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "use_radial",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "x",
            Float,
            added_in=1,
            default="0.0f",
            short_desc="Horizontal Offset",
            long_desc="Horizontal offset from the centered position, in meters (0 = centered).",
            visible=True,
            developer=False
        ),
        # y was an absolute vertical offset; it is now computed every frame from
        # the camera vertical FOV (see Hotbar::update_node_transform), so the
        # field is removed in version 2.
        field(
            "y",
            Float,
            added_in=1,
            removed_in=2,
            default="-0.140f",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "z",
            Float,
            added_in=1,
            default="-0.5f",
            short_desc="Distance",
            long_desc="Distance in front of the camera, in meters (negative = forward).",
            visible=True,
            developer=False
        ),
        field(
            "anchor",
            EnumRef("Hotbar_anchor"),
            added_in=2,
            default="Hotbar_anchor::platform_default",
            short_desc="Anchor",
            long_desc="Which vertical FOV plane the hotbar near edge touches (top or bottom). platform_default = bottom on desktop, top in OpenXR.",
            visible=True,
            developer=False
        ),
        # margin was an absolute (meters) inward offset; replaced in v4 by the
        # FOV-consistent fraction-based `padding` below.
        field(
            "margin",
            Float,
            added_in=2,
            removed_in=4,
            default="0.0f",
            short_desc="Margin",
            long_desc="Extra inward offset from the frustum plane, in meters (0 = edge touches the plane). Useful for OpenXR comfort.",
            visible=True,
            developer=False
        ),
        field(
            "height",
            Float,
            added_in=3,
            default="0.06f",
            short_desc="Height",
            long_desc="Apparent hotbar height as a fraction of the viewport vertical extent (0..1). The hotbar quad is scaled to this fraction every frame so it keeps a constant on-screen size regardless of camera FOV.",
            visible=True,
            developer=False,
            ui_min="0.01f",
            ui_max="0.5f"
        ),
        field(
            "padding",
            Float,
            added_in=4,
            default="0.0f",
            short_desc="Padding",
            long_desc="Extra inward gap between the hotbar near edge and the anchored frustum plane, as a fraction of the viewport vertical extent (0 = edge touches the plane). Stays a constant on-screen gap regardless of camera FOV; mostly useful for OpenXR comfort where the FOV extremes are hard to see.",
            visible=True,
            developer=False,
            ui_min="0.0f",
            ui_max="0.5f"
        ),
    ],
)
