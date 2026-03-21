from erhe_codegen import *

struct("Developer_config",
    version=1,
    short_desc="Developer",
    long_desc="",
    developer=False,
    fields=[
        field(
            "enable",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable Developer Mode",
            long_desc="Exposes additional UI, mostly useful only for debugging",
            visible=True,
            developer=False
        ),
    ],
)
