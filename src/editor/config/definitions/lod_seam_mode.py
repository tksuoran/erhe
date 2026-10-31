from erhe_codegen import *

# Whether LOD collapses may cross attribute seams (doc/editor/lod.md).
# Mirrors erhe::primitive::Lod_seams.
enum("Lod_seam_mode",
    value("keep",       0, short_desc="Keep"),
    value("permissive", 1, short_desc="Permissive"),
    underlying_type=UInt,
)
