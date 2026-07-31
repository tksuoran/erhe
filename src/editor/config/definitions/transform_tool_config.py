from erhe_codegen import *

struct("Transform_tool_config",
    version=7,
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
        field(
            "rotate_ring_size",
            Float,
            added_in=4,
            default="4.0f",
            short_desc="Active Rotate Ring Size",
            long_desc="Radius of the protractor ring shown during an active rotation drag, in the same view-scaled units as the gizmo handles (the gizmo's own rotate rings use 4.0).",
            visible=True,
            developer=False
        ),
        field(
            "rotate_sector_anchoring",
            EnumRef("Rotate_sector_anchoring"),
            added_in=5,
            default="Rotate_sector_anchoring::drag_relative",
            short_desc="Rotation Sector Anchoring",
            long_desc="Where the rotation protractor's swept sector is anchored: Drag Relative starts the sector at the pointer position at drag start; Axis Absolute anchors the protractor to the active coordinate space, placing the initial and current spokes at the anchor's absolute twist angles around the rotation axis.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_snap_absolute",
            Bool,
            added_in=6,
            default="true",
            short_desc="Absolute Rotation Snap",
            long_desc="When rotation snapping is active, land the resulting absolute angle around the rotation axis on snap multiples. When disabled, the drag delta itself is snapped instead, preserving any initial off-grid angle.",
            visible=True,
            developer=False
        ),
        field(
            "translate_snap_absolute",
            Bool,
            added_in=7,
            default="true",
            short_desc="Absolute Translate Snap",
            long_desc="When translate snapping is active, land the anchor's resulting position coordinates on snap multiples. When disabled, the drag delta itself is snapped instead, preserving any initial off-grid position.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_view_ring",
            Bool,
            added_in=7,
            default="true",
            short_desc="View Rotate Ring",
            long_desc="Show an extra light gray camera-aligned ring outside the rotate rings; dragging it rotates around the viewing axis.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_arcball",
            Bool,
            added_in=7,
            default="true",
            short_desc="Free Rotate (Arcball)",
            long_desc="Dragging inside the rotate sphere without hitting any other handle rotates freely, arcball style.",
            visible=True,
            developer=False
        ),
        field(
            "translate_cast_rays",
            Bool,
            added_in=7,
            default="false",
            short_desc="Translate Drag Cast Rays",
            long_desc="During an active translate drag, cast rays from each dragged node along the world x/y/z axes and visualize the nearest hits with purple lines, like the physics tool's drag debug visualization.",
            visible=True,
            developer=False
        ),
    ],
)
