from erhe_codegen import *

struct("Shader_monitor_config",
    version=1,
    short_desc="",
    long_desc="",
    developer=False,
    fields=[
        field(
            "enabled",
            Bool,
            added_in=1,
            default="true",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
    ],
)
