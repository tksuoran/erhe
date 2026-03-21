from erhe_codegen import *

struct("Text_renderer_config",
    field("enabled",   Bool, added_in=1, default="true"),
    field("font_size", Int,  added_in=1, default="14"),
    version=1,
)
