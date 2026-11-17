from erhe_codegen import *

# The shared surface pair, the connector snap distance, its axis mode and its orientation (doc/editor/level_tools.md
# "Surface settings"): kit placement, the Level Kit window, the viewport toolbar and
# the Transform tool's Move settings read and write it.
struct("Surface_snap_config",
    version=5,
    short_desc="Surface Snap",
    long_desc="Snap To Surface and Orient To Surface: kit placement and the Transform tool's translate drags snap moved meshes to the surface under them.",
    developer=False,
    fields=[
        field("snap_to_surface",   Bool, added_in=1, default="false", short_desc="Snap To Surface",   long_desc="Snap a placed piece's base, or a moved mesh's base, to the surface under it."),
        field("orient_to_surface", Bool, added_in=1, default="false", short_desc="Orient To Surface", long_desc="Turn a placed piece's or a moved mesh's up axis to the surface normal, keeping its heading."),
        field("max_distance",      Float, added_in=3, default="1.0f", short_desc="Surface Max Distance", long_desc="Meters: snap to surface moves a mesh at most this far along the snap direction, either way, to the nearest place where it rests and fits; beyond, it stays."),
        field("connector_snap_distance", Float, added_in=2, default="0.0f", short_desc="Connector Snap", long_desc="Meters: a Transform tool drag (translate, rotate or scale) snaps the moved nodes so that a connector of theirs within this distance of another kit piece's connector meets it; 0 disables."),
        field("connector_snap_axis_mode", EnumRef("Connector_snap_axis_mode"), added_in=4, default="Connector_snap_axis_mode::full_offset", short_desc="Connector Snap On Axis", long_desc="An axis or plane drag applies the full connector snap offset, or only its part along the dragged axis or in the dragged plane."),
        field("connector_snap_orientation", EnumRef("Connector_snap_orientation"), added_in=5, default="Connector_snap_orientation::keep", short_desc="Connector Snap Orientation", long_desc="A translate drag's connector snap also turns the moved nodes about the moved connector so the two connector frames face each other, as a connector join does."),
    ],
)
