from erhe_codegen import *

struct("Graphics_preset_entry",
    version=1,
    short_desc="Graphics quality preset",
    long_desc="",
    developer=False,
    fields=[
        field("name",               String, added_in=1, default='""'),
        field("msaa_sample_count",  Int,    added_in=1, default="4"),
        field("bindless_textures",  Bool,   added_in=1, default="false"),
        field("reverse_depth",      Bool,   added_in=1, default="true"),
        field("shadow_enable",      Bool,   added_in=1, default="true"),
        field("shadow_resolution",  Int,    added_in=1, default="1024"),
        field("shadow_light_count", Int,    added_in=1, default="4"),
        field("shadow_depth_bits",  Int,    added_in=1, default="16"),
    ],
)
