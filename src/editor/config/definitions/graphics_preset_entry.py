from erhe_codegen import *

struct("Graphics_preset_entry",
    version=6,
    short_desc="Graphics quality preset",
    long_desc="",
    developer=False,
    fields=[
        field("name",                       String,                      added_in=1, default='""'),
        field("msaa_sample_count",          Int,                         added_in=1, default="4"),
        field("bindless_textures",          Bool,                        added_in=1, default="false"),
        field("shadow_enable",              Bool,                        added_in=1, default="true"),
        field("shadow_resolution",          Int,                         added_in=1, default="1024"),
        field("shadow_light_count",         Int,                         added_in=1, default="4"),
        field("shadow_depth_bits",          Int,                         added_in=1, default="16"),
        field("shadow_filter",              EnumRef("Shadow_filter_mode"), added_in=2, default="Shadow_filter_mode::pcf_2x2",     short_desc="Shadow Filtering"),
        field("shadow_bias",                EnumRef("Shadow_bias_mode"),   added_in=3, default="Shadow_bias_mode::receiver_plane", short_desc="Shadow Bias"),
        field("shadow_depth_bias_constant", Float,                       added_in=4, default="0.0f", short_desc="Shadow Depth Bias (constant)"),
        field("shadow_depth_bias_slope",    Float,                       added_in=4, default="0.0f", short_desc="Shadow Depth Bias (slope)"),
        field("shadow_cull_mode",           EnumRef("Shadow_cull_mode"), added_in=5, default="Shadow_cull_mode::cull_front",      short_desc="Shadow Cull Mode"),
        field("shadow_technique",           EnumRef("Shadow_technique_mode"), added_in=6, default="Shadow_technique_mode::depth", short_desc="Shadow Technique"),
    ],
)
