from erhe_codegen import *

struct("Renderdoc_config",
    version=1,
    short_desc="",
    long_desc="",
    fields=[
        field(
            "capture_support",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True
        ),
    ],
)
