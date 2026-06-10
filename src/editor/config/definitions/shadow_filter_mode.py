from erhe_codegen import *

# Shadow map filtering method. The numeric value IS the PCF kernel width in
# texels (0 = hard / single hardware compare), which the shader uses directly
# as the ERHE_SHADOW_FILTER compile-time variant axis. A KxK PCF kernel is
# fetched with (K/2)^2 textureGather() calls, so only even widths are used.
# Keep these values and the ERHE_SHADOW_FILTER_* handling in
# res/shaders/erhe_light.glsl in sync.
enum("Shadow_filter_mode",
    value("hard",    0, short_desc="Hard (hardware compare, no filtering)"),
    value("pcf_2x2", 2, short_desc="PCF 2x2 (1x textureGather, bilinear)"),
    value("pcf_4x4", 4, short_desc="PCF 4x4 (4x textureGather)"),
    value("pcf_6x6", 6, short_desc="PCF 6x6 (9x textureGather)"),
    underlying_type=UInt,
)
