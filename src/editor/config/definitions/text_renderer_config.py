from erhe_codegen import *

struct("Text_renderer_config",
    version=1,
    short_desc="",
    long_desc="",
    fields=[
        field(
            "enabled",
            Bool,
            added_in=1,
            default="true",
            short_desc="",
            long_desc="",
            visible=True
        ),
        field(
            "font_size",
            Int,
            added_in=1,
            default="14",
            short_desc="",
            long_desc="",
            visible=True
        ),
    ],
)
