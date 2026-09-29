from erhe_codegen import *

# Shadow technique: how the shadow map is generated and sampled. The value is
# the ERHE_SHADOW_TECHNIQUE compile-time variant axis; keep it in sync with the
# ERHE_SHADOW_TECHNIQUE_* handling in res/shaders/erhe_light.glsl.
#   depth    = hardware depth map; the shading pass compares the receiver
#              plane's depth at each texel centre (RPDB) with derived error
#              bounds. The default; see doc/erhe/shadows.md.
#   distance = R32F distance map: the shadow pass stores each caster plane's
#              light distance on the texel centre ray (directional and spot
#              lights), and the shading pass compares the receiver plane's
#              distance on the same rays with derived error bounds
#              (doc/erhe/shadows.md "The distance technique"). Point lights
#              use their distance cube with either value.
enum("Shadow_technique_mode",
    value("depth",    0, short_desc="Depth + receiver-plane bias (default)"),
    value("distance", 1, short_desc="Distance map of caster plane distances (directional, spot)"),
    underlying_type=UInt,
)
