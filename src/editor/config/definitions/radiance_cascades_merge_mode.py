from erhe_codegen import *

# How rc_merge.comp blends a cascade with the cascade above
# (doc/editor/radiance_cascades.md "Merge", doc/plans/radiance_cascades.md
# section 5). The default (Radiance_cascades_config::merge_mode,
# per_neighbour_trace) is the mode that passed the most gi_verify gates,
# doc/plans/radiance_cascades.md section 10 "Merge mode default".
enum("Radiance_cascades_merge_mode",
    value("interpolate",       0, short_desc="Interpolate (trilinear over the 8 upper probes)"),
    value("visibility_masked", 1, short_desc="Visibility masked (skip upper probes the probe cannot see)"),
    value("per_neighbour_trace", 2, short_desc="Per-neighbour trace (trace to each upper probe's interval start)"),
    underlying_type=UInt,
)
