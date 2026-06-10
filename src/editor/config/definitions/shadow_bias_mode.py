from erhe_codegen import *

# Depth-bias method used by the wide-kernel (4x4, 6x6, ...) PCF path. The value
# is the ERHE_SHADOW_BIAS compile-time variant axis; keep it in sync with the
# ERHE_SHADOW_BIAS_* handling in res/shaders/erhe_light.glsl. Exposed so the two
# methods can be compared live; the hard and 2x2 paths are unaffected.
enum("Shadow_bias_mode",
    value("slope_scaled",   0, short_desc="Per-tap slope-scaled (peter-pans on wide kernels)"),
    value("receiver_plane", 1, short_desc="Receiver-plane (contact stays attached)"),
    underlying_type=UInt,
)
