from erhe_codegen import *

struct("Transform_tool_config",
    version=9,
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
            "ring_pick_radius",
            Float,
            added_in=8,
            default="0.2f",
            short_desc="Ring Pick Radius",
            long_desc="Pick tube radius around the rotate rings (axis rings and the view ring), in the same view-scaled units as the gizmo handles (the rings' own radius is 4.0). Larger values make the rings easier to hit, at the cost of the rings' pick zone reaching further over neighboring content.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_shaft_length",
            Float,
            added_in=9,
            default="2.75f",
            short_desc="Arrow Shaft Length",
            long_desc="Length of the translate/scale arrow shafts, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_cone_length",
            Float,
            added_in=9,
            default="0.6f",
            short_desc="Scale Cone Length",
            long_desc="Length of the scale-axis tip cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_cone_radius",
            Float,
            added_in=9,
            default="0.15f",
            short_desc="Scale Cone Radius",
            long_desc="Base radius of the scale-axis tip cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "translate_cone_length",
            Float,
            added_in=9,
            default="1.2f",
            short_desc="Translate Cone Length",
            long_desc="Length of the translate arrow tip cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "translate_cone_radius",
            Float,
            added_in=9,
            default="0.6f",
            short_desc="Translate Cone Radius",
            long_desc="Base radius of the translate arrow tip cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "plane_half_extent",
            Float,
            added_in=9,
            default="1.2f",
            short_desc="Plane Quad Half Extent",
            long_desc="Half extent of the plane translate quads, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "plane_pick_half_extent",
            Float,
            added_in=9,
            default="1.56f",
            short_desc="Plane Quad Pick Half Extent",
            long_desc="Half extent of the plane translate quads' pick area, in view-scaled gizmo units. Larger than the drawn quad so the quads remain easy to hit.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_ring_radius",
            Float,
            added_in=9,
            default="4.0f",
            short_desc="Rotate Ring Radius",
            long_desc="Radius of the gizmo's rotate rings (and the rotate sphere), in view-scaled gizmo units. Translate arrows start just outside this radius.",
            visible=True,
            developer=False
        ),
        field(
            "view_ring_radius_factor",
            Float,
            added_in=9,
            default="1.3f",
            short_desc="View Ring Radius Factor",
            long_desc="Radius of the camera-aligned view-rotate ring as a multiple of the rotate ring radius.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_ring_gap",
            Float,
            added_in=9,
            default="0.25f",
            short_desc="Arrow Ring Gap",
            long_desc="Gap between the rotate sphere and the start of the translate arrows, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "center_cube_half_length",
            Float,
            added_in=9,
            default="0.25f",
            short_desc="Center Cube Half Length",
            long_desc="Half edge length of the uniform-scale center cube, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "center_cube_pick_radius",
            Float,
            added_in=9,
            default="0.5f",
            short_desc="Center Cube Pick Radius",
            long_desc="Pick sphere radius of the uniform-scale center cube, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "box_scale_cone_length",
            Float,
            added_in=9,
            default="0.6f",
            short_desc="Box Scale Cone Length",
            long_desc="Length of the bounding-box scale face cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "box_scale_cone_radius",
            Float,
            added_in=9,
            default="0.2f",
            short_desc="Box Scale Cone Radius",
            long_desc="Base radius of the bounding-box scale face cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_shaft_pick_radius",
            Float,
            added_in=9,
            default="0.12f",
            short_desc="Arrow Shaft Pick Radius",
            long_desc="Pick capsule radius around the translate arrow shafts, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_head_pick_radius",
            Float,
            added_in=9,
            default="0.45f",
            short_desc="Scale Head Pick Radius",
            long_desc="Pick capsule radius around the scale-axis tip cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "translate_head_pick_radius",
            Float,
            added_in=9,
            default="0.9f",
            short_desc="Translate Head Pick Radius",
            long_desc="Pick capsule radius around the translate arrow tip cones, in view-scaled gizmo units.",
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
