from erhe_codegen import *

# Shadow depth-bias method, the ERHE_SHADOW_BIAS compile-time variant axis;
# keep it in sync with the ERHE_SHADOW_BIAS_* handling in
# res/shaders/erhe_light.glsl. slope_scaled and receiver_plane are the
# derived-bound lookups and differ only on the wide-kernel (4x4, 6x6, ...)
# PCF path; normal_offset is the one-fetch lookup with a normal offset and a
# constant bias in shadow texels (doc/erhe/shadows.md "Normal offset bias").
enum("Shadow_bias_mode",
    value("slope_scaled",   0, short_desc="Per-tap slope-scaled (peter-pans on wide kernels)"),
    value("receiver_plane", 1, short_desc="Receiver-plane (contact stays attached)"),
    value("normal_offset",  2, short_desc="Normal offset, one hardware filtered fetch (mobile)"),
    underlying_type=UInt,
)
