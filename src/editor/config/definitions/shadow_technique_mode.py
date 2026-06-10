from erhe_codegen import *

# Shadow technique: how the shadow map is generated and sampled. The value is
# the ERHE_SHADOW_TECHNIQUE compile-time variant axis; keep it in sync with the
# ERHE_SHADOW_TECHNIQUE_* handling in res/shaders/erhe_light.glsl.
#   depth    = hardware depth map + receiver-plane depth bias (RPDB) applied in
#              the shading pass. The default; see doc/shadows.md.
#   distance = "bias-free" map: the shadow pass stores a linear distance with a
#              fwidth slope bias baked in, and the shading pass compares without
#              any receiver-side bias. Directional lights only for now.
enum("Shadow_technique_mode",
    value("depth",    0, short_desc="Depth + receiver-plane bias (default)"),
    value("distance", 1, short_desc="Distance map + baked fwidth bias (bias-free)"),
    underlying_type=UInt,
)
