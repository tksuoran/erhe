from erhe_codegen import *

struct("Id_renderer_config",
    version=1,
    short_desc="ID Renderer",
    long_desc="",
    developer=False,
    fields=[
        field(
            "enabled",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable ID Renderer",
            long_desc="",
            visible=True,
            developer=False
        ),
    ],
)
