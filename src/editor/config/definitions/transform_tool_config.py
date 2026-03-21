from erhe_codegen import *

struct("Transform_tool_config",
    version=1,
    short_desc="",
    long_desc="",
    fields=[
        field(
            "show_translate",
            Bool,
            added_in=1,
            default="true",
            short_desc="",
            long_desc="",
            visible=True
        ),
        field(
            "show_rotate",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True
        ),
    ],
)
