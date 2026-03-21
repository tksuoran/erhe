from erhe_codegen import *

struct("Transform_tool_config",
    field("show_translate", Bool, added_in=1, default="true"),
    field("show_rotate",    Bool, added_in=1, default="false"),
    version=1,
)
