from erhe_codegen import *

struct("Graphics_config",
    field("initial_clear",                     Bool, added_in=1, default="true"),
    field("post_processing",                   Bool, added_in=1, default="true"),
    field("force_bindless_textures_off",       Bool, added_in=1, default="false"),
    field("force_no_persistent_buffers",       Bool, added_in=1, default="false"),
    field("force_no_direct_state_access",      Bool, added_in=1, default="false"),
    field("force_emulate_multi_draw_indirect", Bool, added_in=1, default="false"),
    field("force_gl_version",                  Int,  added_in=1, default="0"),
    field("force_glsl_version",                Int,  added_in=1, default="0"),
    version=1,
)
