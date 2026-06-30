from erhe_codegen import *

# Vertical anchor for the editor Hotbar billboard (centered horizontally, faces
# the camera). Selects which horizontal frustum plane the hotbar near edge
# touches. platform_default resolves to bottom on desktop and top in OpenXR
# (resolved in Hotbar's constructor). Keep in sync with hotbar.cpp.
enum("Hotbar_anchor",
    value("platform_default", 0, short_desc="Bottom on desktop, top in OpenXR"),
    value("bottom",           1, short_desc="Anchor to bottom of vertical FOV"),
    value("top",              2, short_desc="Anchor to top of vertical FOV"),
    underlying_type=UInt,
)
