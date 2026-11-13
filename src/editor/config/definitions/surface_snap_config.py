from erhe_codegen import *

# The shared surface pair (doc/editor/level_tools.md "Surface settings"): kit
# placement, the Level Kit window, the viewport toolbar and the Transform tool's
# Move settings read and write it.
struct("Surface_snap_config",
    version=1,
    short_desc="Surface Snap",
    long_desc="Snap To Surface and Orient To Surface: kit placement and the Transform tool's translate drags rest moved meshes on the surface under them.",
    developer=False,
    fields=[
        field("snap_to_surface",   Bool, added_in=1, default="false", short_desc="Snap To Surface",   long_desc="Rest a placed piece's base, or a moved mesh's base, on the surface under it."),
        field("orient_to_surface", Bool, added_in=1, default="false", short_desc="Orient To Surface", long_desc="Turn a placed piece's or a moved mesh's up axis to the surface normal, keeping its heading."),
    ],
)
