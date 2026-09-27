from erhe_codegen import *

# How rc_merge.comp blends a cascade with the cascade above
# (doc/editor/radiance_cascades.md "Merge", doc/plans/radiance_cascades.md
# section 5). The default is chosen after plan phase 4 from the
# surface-level gi_verify accuracy.
enum("Radiance_cascades_merge_mode",
    value("interpolate",       0, short_desc="Interpolate (trilinear over the 8 upper probes)"),
    value("visibility_masked", 1, short_desc="Visibility masked (skip upper probes the probe cannot see)"),
    underlying_type=UInt,
)
