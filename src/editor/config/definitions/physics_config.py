from erhe_codegen import *

struct("Physics_config",
    field(
        "static_enable",
        Bool,
        added_in=1,
        default="true",
        short_desc="",
        long_desc=""
    ),
    field(
        "dynamic_enable",
        Bool,
        added_in=1,
        default="true",
        short_desc="",
        long_desc=""
    ),
    version=1,
    short_desc="",
    long_desc="",
)
