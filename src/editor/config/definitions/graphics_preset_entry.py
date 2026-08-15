from erhe_codegen import *

struct("Graphics_preset_entry",
    version=2,
    short_desc="Graphics quality preset",
    long_desc="",
    developer=False,
    fields=[
        field("name",                       String,                      added_in=1, default='""'),
        field("msaa_sample_count",          Int,                         added_in=1, default="4"),
        field("bindless_textures",          Bool,                        added_in=1, default="false"),
        field("shadow_enable",              Bool,                        added_in=1, default="true"),
        field("shadow_resolution",          Int,                         added_in=1, default="1024"),
        field("shadow_depth_bits",          Int,                         added_in=1, default="16"),
        field("shadow_filter",              EnumRef("Shadow_filter_mode"), added_in=1, default="Shadow_filter_mode::pcf_2x2",     short_desc="Shadow Filtering"),
        field("shadow_bias",                EnumRef("Shadow_bias_mode"),   added_in=1, default="Shadow_bias_mode::receiver_plane", short_desc="Shadow Bias"),
        field("shadow_depth_bias_constant", Float,                       added_in=1, default="0.0f", short_desc="Shadow Depth Bias (constant)"),
        field("shadow_depth_bias_slope",    Float,                       added_in=1, default="0.0f", short_desc="Shadow Depth Bias (slope)"),
        field("shadow_cull_mode",           EnumRef("Shadow_cull_mode"), added_in=1, default="Shadow_cull_mode::cull_front",      short_desc="Shadow Cull Mode"),
        field("shadow_technique",           EnumRef("Shadow_technique_mode"), added_in=1, default="Shadow_technique_mode::depth", short_desc="Shadow Technique"),
        # Point lights cast omnidirectional shadows into an R32F cube-map array
        # (one cube / 6 faces per shadow-casting point light). These bound that
        # array independently of the 2D directional/spot shadow map because R32F
        # cube arrays are memory-heavy (a 512^2 cube is ~6 MB).
        field("point_shadow_resolution",    Int,                         added_in=1, default="512", short_desc="Point Shadow Resolution"),
        # Per light type light count limits (v2): how many lights of each type
        # are shadow-mapped, and how many more are shaded without a shadow map.
        # Directional + spot shadow lights share the 2D shadow map array (its
        # layer count is their sum); point shadow lights each get a cube in the
        # R32F cube-map array. Lights beyond a limit are not shaded at all;
        # shadow casters beyond the shadow limit are shaded unshadowed while the
        # unshadowed limit has room. With shadow_enable off every light is
        # shaded unshadowed (the shadow limits count as 0).
        field("directional_shadow_light_count",     Int, added_in=2, default="2", short_desc="Directional Shadow Lights"),
        field("directional_unshadowed_light_count", Int, added_in=2, default="4", short_desc="Directional Lights (no shadow)"),
        field("spot_shadow_light_count",            Int, added_in=2, default="2", short_desc="Spot Shadow Lights"),
        field("spot_unshadowed_light_count",        Int, added_in=2, default="4", short_desc="Spot Lights (no shadow)"),
        field("point_shadow_light_count",           Int, added_in=1, default="2", short_desc="Point Shadow Lights"),
        field("point_unshadowed_light_count",       Int, added_in=2, default="4", short_desc="Point Lights (no shadow)"),
    ],
)
