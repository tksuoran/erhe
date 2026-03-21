from erhe_codegen import *

struct("Transform_tool_config",
    field(
        "show_translate",
        Bool,
        added_in=1,
        default="true",
        short_desc="",
        long_desc=""
    ),
    field(
        "show_rotate",
        Bool,
        added_in=1,
        default="false",
        short_desc="",
        long_desc=""
    ),
    version=1,
    short_desc="",
    long_desc="",
)
