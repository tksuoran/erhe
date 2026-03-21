from erhe_codegen import *

struct("Renderdoc_config",
    version=1,
    short_desc="RenderDoc",
    long_desc="",
    developer=False,
    fields=[
        field(
            "capture_support",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable RenderDoc Capture",
            long_desc="",
            visible=True,
            developer=False
        ),
    ],
)
