from erhe_codegen import *

struct("Headset_config",
    field("openxr",            Bool, added_in=1, default="false"),
    field("openxr_mirror",     Bool, added_in=1, default="false"),
    field("quad_view",         Bool, added_in=1, default="false"),
    field("debug",             Bool, added_in=1, default="false"),
    field("validation",        Bool, added_in=1, default="false"),
    field("api_dump",          Bool, added_in=1, default="false"),
    field("depth",             Bool, added_in=1, default="false"),
    field("visibility_mask",   Bool, added_in=1, default="false"),
    field("hand_tracking",     Bool, added_in=1, default="false"),
    field("passthrough_fb",    Bool, added_in=1, default="false"),
    field("composition_alpha", Bool, added_in=1, default="false"),
    version=1,
)
