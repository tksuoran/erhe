from erhe_codegen import *

# Where rc_trace.comp aims the ray of a raw texel (doc/editor/radiance_cascades.md
# "Trace"): the octahedral texel centre every update, or a new random point
# of the texel footprint every update, which the hysteresis blend averages
# into the footprint mean.
enum("Radiance_cascades_direction_jitter",
    value("none",      0, short_desc="None (texel centre direction)"),
    value("footprint", 1, short_desc="Footprint (random point of the texel footprint per update)"),
    underlying_type=UInt,
)
