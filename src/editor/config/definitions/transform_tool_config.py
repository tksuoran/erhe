from erhe_codegen import *

struct("Transform_tool_config",
    reflect=True,
    version=1,
    short_desc="Transform Tool",
    long_desc="",
    developer=False,
    fields=[
        field(
            "gizmo_scale",
            Float,
            added_in=1,
            default="4.5f",
            short_desc="Gizmo Scale",
            long_desc="Scale factor for the transform gizmo handles (moved here from the Viewport group in Editor Settings).",
            visible=True,
            developer=False
        ),
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
            "show_scale",
            Bool,
            added_in=1,
            default="true",
            short_desc="Show Scale",
            long_desc="Show the scale gizmo at startup.",
            visible=True,
            developer=False
        ),
        field(
            "translate_negative_handles",
            Bool,
            added_in=1,
            default="true",
            short_desc="Negative Translate Handles",
            long_desc="Show the negative-direction translation axis handles in addition to the positive ones. When disabled, only the positive-direction handles are shown.",
            visible=True,
            developer=False
        ),
        field(
            "hover_preview",
            Bool,
            added_in=1,
            default="true",
            short_desc="Hover Preview",
            long_desc="When hovering a gizmo handle, preview its constraint: the axis line for an axis translation handle, the plane rectangle and grid for a plane translation handle, and the rotation axis plus rotation plane for a rotation ring.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_visible_arcs_only",
            Bool,
            added_in=1,
            default="false",
            short_desc="Visible Ring Arcs Only",
            long_desc="Draw the rotation rings as a ball of three discs: where a ring passes behind the discs of the other rings it is drawn as a thin reference line and is not hoverable or draggable.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_ring_size",
            Float,
            added_in=1,
            default="4.0f",
            short_desc="Active Rotate Ring Size",
            long_desc="Radius of the protractor ring shown during an active rotation drag, in the same view-scaled units as the gizmo handles (the gizmo's own rotate rings use 4.0).",
            visible=True,
            developer=False
        ),
        field(
            "rotate_sector_anchoring",
            EnumRef("Rotate_sector_anchoring"),
            added_in=1,
            default="Rotate_sector_anchoring::drag_relative",
            short_desc="Rotation Sector Anchoring",
            long_desc="Where the rotation protractor's swept sector is anchored: Drag Relative starts the sector at the pointer position at drag start; Axis Absolute anchors the protractor to the active coordinate space, placing the initial and current spokes at the anchor's absolute twist angles around the rotation axis.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_snap_absolute",
            Bool,
            added_in=1,
            default="true",
            short_desc="Absolute Rotation Snap",
            long_desc="When rotation snapping is active, land the resulting absolute angle around the rotation axis on snap multiples. When disabled, the drag delta itself is snapped instead, preserving any initial off-grid angle.",
            visible=True,
            developer=False
        ),
        field(
            "translate_snap_absolute",
            Bool,
            added_in=1,
            default="true",
            short_desc="Absolute Translate Snap",
            long_desc="When translate snapping is active, land the anchor's resulting position coordinates on snap multiples. When disabled, the drag delta itself is snapped instead, preserving any initial off-grid position.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_view_ring",
            Bool,
            added_in=1,
            default="true",
            short_desc="View Rotate Ring",
            long_desc="Show an extra light gray camera-aligned ring outside the rotate rings; dragging it rotates around the viewing axis.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_arcball",
            Bool,
            added_in=1,
            default="true",
            short_desc="Free Rotate (Arcball)",
            long_desc="Dragging inside the rotate sphere without hitting any other handle rotates freely, arcball style.",
            visible=True,
            developer=False
        ),
        field(
            "ring_pick_radius",
            Float,
            added_in=1,
            default="0.2f",
            short_desc="Ring Pick Radius",
            long_desc="Pick tube radius around the rotate rings (axis rings and the view ring), in the same view-scaled units as the gizmo handles (the rings' own radius is 4.0). Larger values make the rings easier to hit, at the cost of the rings' pick zone reaching further over neighboring content.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_shaft_length",
            Float,
            added_in=1,
            default="2.75f",
            short_desc="Arrow Shaft Length",
            long_desc="Length of the translate/scale arrow shafts, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "translate_cone_length",
            Float,
            added_in=1,
            default="1.2f",
            short_desc="Translate Cone Length",
            long_desc="Length of the translate arrow tip cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "translate_cone_radius",
            Float,
            added_in=1,
            default="0.6f",
            short_desc="Translate Cone Radius",
            long_desc="Base radius of the translate arrow tip cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_ring_radius",
            Float,
            added_in=1,
            default="4.0f",
            short_desc="Rotate Ring Radius",
            long_desc="Radius of the gizmo's rotate rings (and the rotate sphere), in view-scaled gizmo units. Translate arrows start just outside this radius.",
            visible=True,
            developer=False
        ),
        field(
            "view_ring_radius_factor",
            Float,
            added_in=1,
            default="1.3f",
            short_desc="View Ring Radius Factor",
            long_desc="Radius of the camera-aligned view-rotate ring as a multiple of the rotate ring radius.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_ring_gap",
            Float,
            added_in=1,
            default="0.25f",
            short_desc="Arrow Ring Gap",
            long_desc="Gap between the rotate sphere and the start of the translate arrows, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "box_scale_cone_length",
            Float,
            added_in=1,
            default="0.6f",
            short_desc="Box Scale Cone Length",
            long_desc="Length of the bounding-box scale face cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "box_scale_cone_radius",
            Float,
            added_in=1,
            default="0.2f",
            short_desc="Box Scale Cone Radius",
            long_desc="Base radius of the bounding-box scale face cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_shaft_pick_radius",
            Float,
            added_in=1,
            default="0.12f",
            short_desc="Arrow Shaft Pick Radius",
            long_desc="Pick capsule radius around the translate arrow shafts, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "arrow_head_pick_radius",
            Float,
            added_in=1,
            default="0.45f",
            short_desc="Scale Head Pick Radius",
            long_desc="Pick capsule radius around the scale-axis tip cubes, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "translate_head_pick_radius",
            Float,
            added_in=1,
            default="0.9f",
            short_desc="Translate Head Pick Radius",
            long_desc="Pick capsule radius around the translate arrow tip cones, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "scale_shaft_length",
            Float,
            added_in=1,
            default="0.8f",
            short_desc="Scale Shaft Length",
            long_desc="Shaft run of the scale-axis handles when the translate arrows are also shown (the scale handle continues the axis line past the translate cone tip), in view-scaled gizmo units. With translate hidden the scale handles use the full Arrow Shaft Length instead.",
            visible=True,
            developer=False
        ),
        field(
            "scale_cube_half_length",
            Float,
            added_in=1,
            default="0.35f",
            short_desc="Scale Cube Half Length",
            long_desc="Half edge length of the scale-axis tip cubes, in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "scale_handle_gap",
            Float,
            added_in=1,
            default="0.25f",
            short_desc="Scale Handle Gap",
            long_desc="Gap between a translate handle and the scale handle that continues past it (axis: past the translate cone tip; plane: past the translate quad's outer corner), in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "uniform_scale_outer_radius",
            Float,
            added_in=1,
            default="0.5f",
            short_desc="Uniform Scale Outer Radius",
            long_desc="Outer radius of the innermost plane sectors, which together form the uniform-scale handle (one wedge per plane, from the gizmo center), in view-scaled gizmo units.",
            visible=True,
            developer=False
        ),
        field(
            "plane_sector_gap",
            Float,
            added_in=1,
            default="0.2f",
            short_desc="Plane Sector Gap",
            long_desc="Radial gap between the concentric plane-handle shells (center sphere, plane-translate sector, plane-scale sector, rotate ring), in view-scaled gizmo units. Each sector's pick zone grows by half this gap, so pick zones never overlap.",
            visible=True,
            developer=False
        ),
        field(
            "plane_translate_outer_radius",
            Float,
            added_in=1,
            default="2.4f",
            short_desc="Plane Translate Outer Radius",
            long_desc="Outer radius of the plane-translate annular sectors, in view-scaled gizmo units. The sectors start at the center sphere plus the sector gap; the plane-scale sectors span from here plus the gap out to the rotate ring radius minus the gap.",
            visible=True,
            developer=False
        ),
        field(
            "ring_sector_gap",
            Float,
            added_in=1,
            default="0.5f",
            short_desc="Ring Sector Gap",
            long_desc="Radial gap between the rotate ring and the outermost plane-handle sector inside it, in view-scaled gizmo units. Larger than the Plane Sector Gap between shells so the ring stays visually separated from the sectors.",
            visible=True,
            developer=False
        ),
        field(
            "handle_line_width",
            Float,
            added_in=1,
            default="3.0f",
            short_desc="Handle Line Width",
            long_desc="Base line width of the gizmo handles, in screen-space pixels. The translate/scale arrow shafts draw at half this width (thin stem + wide tip reads as a pointer). The rotate rings have their own Ring Line Width settings.",
            visible=True,
            developer=False
        ),
        field(
            "handle_line_width_hot",
            Float,
            added_in=1,
            default="4.5f",
            short_desc="Handle Line Width (Hot)",
            long_desc="Base line width of the hovered/active gizmo handle, in screen-space pixels. Arrow shafts draw at half this width. The rotate rings have their own Ring Line Width settings.",
            visible=True,
            developer=False
        ),
        field(
            "plane_outline_width",
            Float,
            added_in=1,
            default="2.0f",
            short_desc="Plane Sector Line Width",
            long_desc="Line width of the plane-handle sector outlines and the bounding-box scale box outline, in screen-space pixels. Independent of Handle Line Width, which drives the arrows and rings.",
            visible=True,
            developer=False
        ),
        field(
            "plane_fill_alpha",
            Float,
            added_in=1,
            default="0.5f",
            short_desc="Plane Sector Fill Alpha",
            long_desc="Fill opacity of the plane-handle sectors (uniform-scale wedges, plane-translate sectors), 0..1. The plane-scale sectors fill at half this value so the two shells read differently.",
            visible=True,
            developer=False
        ),
        field(
            "hover_dim_factor",
            Float,
            added_in=1,
            default="0.4f",
            short_desc="Hover Dim Factor",
            long_desc="Alpha multiplier applied to the other gizmo handles while one handle is hovered (before a drag starts), 0..1. Handles related to the hovered one keep full strength: a plane-translate hover keeps its two axis arrows, a rotate-ring hover keeps the translate arrow along the rotation axis, a uniform-scale hover keeps every scale handle.",
            visible=True,
            developer=False
        ),
        field(
            "axis_color_x",
            Vec4,
            added_in=1,
            default="1.00f, 0.00f, 0.00f, 1.0f",
            short_desc="X Axis Color",
            long_desc="Color of the X-axis gizmo handles (arrow, scale cube, ring, YZ-colored plane sectors use their perpendicular axis color).",
            visible=True,
            developer=False
        ),
        field(
            "axis_color_y",
            Vec4,
            added_in=1,
            default="0.23f, 1.00f, 0.00f, 1.0f",
            short_desc="Y Axis Color",
            long_desc="Color of the Y-axis gizmo handles.",
            visible=True,
            developer=False
        ),
        field(
            "axis_color_z",
            Vec4,
            added_in=1,
            default="0.00f, 0.23f, 1.00f, 1.0f",
            short_desc="Z Axis Color",
            long_desc="Color of the Z-axis gizmo handles.",
            visible=True,
            developer=False
        ),
        field(
            "uniform_scale_color",
            Vec4,
            added_in=1,
            default="0.70f, 0.70f, 0.70f, 1.0f",
            short_desc="Uniform Scale Color",
            long_desc="Fill color of the uniform-scale handle (the three innermost plane wedges).",
            visible=True,
            developer=False
        ),
        field(
            "axis_outline_color_x",
            Vec4,
            added_in=1,
            default="1.00f, 0.00f, 0.00f, 1.0f",
            short_desc="X Axis Outline Color",
            long_desc="Outline color of X-colored plane sectors (the hovered-sector outline). Defaults to the X axis color.",
            visible=True,
            developer=False
        ),
        field(
            "axis_outline_color_y",
            Vec4,
            added_in=1,
            default="0.23f, 1.00f, 0.00f, 1.0f",
            short_desc="Y Axis Outline Color",
            long_desc="Outline color of Y-colored plane sectors. Defaults to the Y axis color.",
            visible=True,
            developer=False
        ),
        field(
            "axis_outline_color_z",
            Vec4,
            added_in=1,
            default="0.00f, 0.23f, 1.00f, 1.0f",
            short_desc="Z Axis Outline Color",
            long_desc="Outline color of Z-colored plane sectors. Defaults to the Z axis color.",
            visible=True,
            developer=False
        ),
        field(
            "uniform_scale_outline_color",
            Vec4,
            added_in=1,
            default="0.70f, 0.70f, 0.70f, 1.0f",
            short_desc="Uniform Scale Outline Color",
            long_desc="Outline color of the uniform-scale wedges when hovered. Defaults to the uniform scale color.",
            visible=True,
            developer=False
        ),
        field(
            "ring_line_width",
            Float,
            added_in=1,
            default="2.25f",
            short_desc="Ring Line Width",
            long_desc="Line width of the rotate rings (axis rings and the view ring) at rest, in screen-space pixels. Previously derived as 0.75x the Handle Line Width.",
            visible=True,
            developer=False
        ),
        field(
            "ring_line_width_hot",
            Float,
            added_in=1,
            default="1.7f",
            short_desc="Ring Line Width (Hot)",
            long_desc="Line width of the hovered/active rotate ring, in screen-space pixels. Thinner than the resting ring by default - the hot emphasis comes from the brightened color, not width; set it higher for a thicker hot ring instead.",
            visible=True,
            developer=False
        ),
        field(
            "rotate_background_alpha",
            Float,
            added_in=1,
            default="0.35f",
            short_desc="Rotate Background Alpha",
            long_desc="Opacity of the black background disc drawn behind the protractor during an active rotate drag, 0..1 (0 disables the disc). The disc is depth-tested against scene content, so it never covers geometry in front of the rotation plane, while all protractor lines draw over it.",
            visible=True,
            developer=False
        ),
        field(
            "active_color_x",
            Vec4,
            added_in=1,
            default="2.0f, 0.0f, 0.0f, 1.0f",
            short_desc="X Active Color",
            long_desc="Drag-time emphasis color for X-constrained elements: the active-rotate protractor around X, the rotate hover ring highlight, and the YZ-plane feedback. Components above 1 over-brighten when blended.",
            visible=True,
            developer=False
        ),
        field(
            "active_color_y",
            Vec4,
            added_in=1,
            default="0.0f, 2.0f, 0.0f, 1.0f",
            short_desc="Y Active Color",
            long_desc="Drag-time emphasis color for Y-constrained elements.",
            visible=True,
            developer=False
        ),
        field(
            "active_color_z",
            Vec4,
            added_in=1,
            default="0.0f, 0.0f, 2.0f, 1.0f",
            short_desc="Z Active Color",
            long_desc="Drag-time emphasis color for Z-constrained elements.",
            visible=True,
            developer=False
        ),
        field(
            "active_color_view",
            Vec4,
            added_in=1,
            default="0.7f, 0.7f, 0.7f, 1.0f",
            short_desc="View/Free Active Color",
            long_desc="Drag-time emphasis color for the view-ring and free (arcball) rotation and any other non-axis constraint.",
            visible=True,
            developer=False
        ),
        field(
            "hover_color_x",
            Vec4,
            added_in=1,
            default="2.0f, 0.0f, 0.0f, 1.0f",
            short_desc="X Hover Color",
            long_desc="Color of a hovered/active X-colored handle (arrow, scale cube, ring, sector fill and outline). Defaults to 2x the X axis color - components above 1 over-brighten when blended.",
            visible=True,
            developer=False
        ),
        field(
            "hover_color_y",
            Vec4,
            added_in=1,
            default="0.46f, 2.0f, 0.0f, 1.0f",
            short_desc="Y Hover Color",
            long_desc="Color of a hovered/active Y-colored handle. Defaults to 2x the Y axis color.",
            visible=True,
            developer=False
        ),
        field(
            "hover_color_z",
            Vec4,
            added_in=1,
            default="0.0f, 0.46f, 2.0f, 1.0f",
            short_desc="Z Hover Color",
            long_desc="Color of a hovered/active Z-colored handle. Defaults to 2x the Z axis color.",
            visible=True,
            developer=False
        ),
        field(
            "hover_uniform_scale_color",
            Vec4,
            added_in=1,
            default="1.4f, 1.4f, 1.4f, 1.0f",
            short_desc="Uniform Scale Hover Color",
            long_desc="Color of the uniform-scale wedges while hovered/active. Defaults to 2x the uniform scale color.",
            visible=True,
            developer=False
        ),
        field(
            "hover_view_ring_color",
            Vec4,
            added_in=1,
            default="1.4f, 1.4f, 1.4f, 1.0f",
            short_desc="View Ring Hover Color",
            long_desc="Color of the view-rotate ring while hovered/active. Defaults to 2x the view ring gray.",
            visible=True,
            developer=False
        ),
        field(
            "translate_cast_rays",
            Bool,
            added_in=1,
            default="false",
            short_desc="Translate Drag Cast Rays",
            long_desc="During an active translate drag, cast rays from each dragged node along the world x/y/z axes and visualize the nearest hits with purple lines, like the physics tool's drag debug visualization.",
            visible=True,
            developer=False
        ),
    ],
)
