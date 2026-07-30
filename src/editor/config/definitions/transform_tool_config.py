from erhe_codegen import *

struct("Transform_tool_config",
    version=3,
    short_desc="Transform Tool",
    long_desc="",
    developer=False,
    fields=[
        field(
            "show_translate",
            Bool,
            added_in=1,
            default="true",
            short_desc="Show Translate",
            long_desc="Show the translate gizmo at startup.",
            visible=True,
            developer=False
        ),
        field(
            "show_rotate",
            Bool,
            added_in=1,
            default="false",
            short_desc="Show Rotate",
            long_desc="Show the rotate gizmo at startup.",
            visible=True,
            developer=False
        ),
        field(
            "translate_negative_handles",
            Bool,
            added_in=2,
            default="true",
            short_desc="Negative Translate Handles",
            long_desc="Show the negative-direction translation axis handles in addition to the positive ones. When disabled, only the positive-direction handles are shown.",
            visible=True,
            developer=False
        ),
        field(
            "hover_preview",
            Bool,
            added_in=2,
            default="true",
            short_desc="Hover Preview",
            long_desc="When hovering a gizmo handle, preview its constraint: the axis line for an axis translation handle, the plane rectangle and grid for a plane translation handle, and the rotation axis plus rotation plane for a rotation ring.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_visible_arcs_only",
            Bool,
            added_in=3,
            default="false",
            short_desc="Visible Ring Arcs Only",
            long_desc="Draw the rotation rings as a ball of three discs: where a ring passes behind the discs of the other rings it is drawn as a thin reference line and is not hoverable or draggable.",
            visible=True,
            developer=False
        ),
    ],
)
