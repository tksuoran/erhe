from erhe_codegen import *

struct("Thumbnails_config",
    field(
        "capacity",
        Int,
        added_in=1,
        default="200",
        short_desc="",
        long_desc=""
    ),
    field(
        "size_pixels",
        Int,
        added_in=1,
        default="256",
        short_desc="",
        long_desc=""
    ),
    version=1,
    short_desc="",
    long_desc="",
)
