from erhe_codegen import *

struct("Hotbar_config",
    version=2,
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
        field(
            "margin",
            Float,
            added_in=2,
            default="0.0f",
            short_desc="Margin",
            long_desc="Extra inward offset from the frustum plane, in meters (0 = edge touches the plane). Useful for OpenXR comfort.",
            visible=True,
            developer=False
        ),
    ],
)
