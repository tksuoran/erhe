from erhe_codegen import *

# The shared surface pair, the rest rule, the drag search direction, the connector snap distance, its axis mode and its orientation (doc/editor/level_tools.md
# "Surface settings"): kit placement, the Level Kit window, the viewport toolbar and
# the Transform tool's Move settings read and write it. v8: connector_snap_orientation off | rotate | full (keep and align read as off and full).
# v9: the connection settings of doc/plans/level_kit_connections.md: auto-connect, the flow mask and the step limit.
# v10: attach_to_surface (doc/plans/level_kit_surface_attachment.md S2.7).
struct("Surface_snap_config",
    version=10,
    short_desc="Surface Snap",
    long_desc="Snap To Surface and Orient To Surface: kit placement snaps placed pieces and the Transform tool's translate drags snap the transform anchor to a surface.",
    developer=False,
    fields=[
        field("snap_to_surface",   Bool, added_in=1, default="false", short_desc="Snap To Surface",   long_desc="Snap a placed piece's base, or a translate drag's transform anchor, to the nearest surface along the search direction."),
        field("orient_to_surface", Bool, added_in=1, default="false", short_desc="Orient To Surface", long_desc="Turn a placed piece's or a moved mesh's up axis to the surface normal, keeping its heading."),
        field("attach_to_surface", Bool, added_in=10, default="false", short_desc="Attach To Surface", long_desc="Placements and drags snap to the surface, and a kit piece snapped onto another kit piece becomes a child of the piece it rests on, remembering its place there."),
        field("max_distance",      Float, added_in=3, default="1.0f", short_desc="Surface Max Distance", long_desc="Meters: snap to surface moves a mesh at most this far along the snap direction, either way, to the nearest place where it rests and fits; beyond, it stays."),
        field("rest", EnumRef("Surface_snap_rest"), added_in=7, default="Surface_snap_rest::leading_face", short_desc="Surface Rest", long_desc="With Orient To Surface off, Transform tool drags and Brush tool kit placement rest the bounds face leading along the search direction flush on the surface, or land the anchor point (the gizmo anchor; the piece origin) on it."),
        field("drag_direction_mode", EnumRef("Surface_drag_direction"), added_in=6, default="Surface_drag_direction::explicit_vector", short_desc="Surface Drag Direction Mode", long_desc="A Transform tool drag snaps the transform anchor along the explicit Surface Drag Direction, or along the ray from the viewport camera through the anchor."),
        field("drag_direction",      Vec3, added_in=6, default="0.0f, -1.0f, 0.0f", short_desc="Surface Drag Direction", long_desc="The direction, in world space, a Transform tool drag snaps the transform anchor along with the explicit mode; normalized on use."),
        field("connector_snap_distance", Float, added_in=2, default="0.0f", short_desc="Connector Snap", long_desc="Meters: a Transform tool drag (translate, rotate or scale) snaps the moved nodes so that a connector of theirs within this distance of another kit piece's connector meets it; 0 disables."),
        field("connector_snap_axis_mode", EnumRef("Connector_snap_axis_mode"), added_in=4, default="Connector_snap_axis_mode::full_offset", short_desc="Connector Snap On Axis", long_desc="An axis or plane drag applies the full connector snap offset, or only its part along the dragged axis or in the dragged plane."),
        field("connector_snap_orientation", EnumRef("Connector_snap_orientation"), added_in=5, default="Connector_snap_orientation::off", short_desc="Connector Snap Orientation", long_desc="Off: a Transform tool drag's connector snap translates only. Full: it also turns the moved nodes about the moved connector so the two connector frames face each other, as a connector join does. Rotate: with Orient To Surface on it turns them only about their up axis, the surface normal; without it as Full."),
        field("connector_auto_connect", Bool, added_in=9, default="true", short_desc="Auto-connect", long_desc="After a change, a connector of a changed kit piece that sits on a free compatible connector of another piece (within 0.1 mm, facing it) is connected (L1)."),
        field("connector_flow_off", Int, added_in=9, default="0", short_desc="Connector Flow Off", long_desc="One bit per connector type switched off (generic 1, wall 2, wall-floor 4, floor 8, rail 16): values and poses flow over a connection only when a type both connectors accept is on (L17, L21); a type added later starts on."),
        field("connection_step_limit", Int, added_in=9, default="64", short_desc="Connection Step Limit", long_desc="The most rounds a change's property flow runs (L13); Auto-connect runs once per change."),
    ],
)
