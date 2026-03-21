from erhe_codegen import *

struct("Threading_config",
    version=1,
    short_desc="",
    long_desc="",
    fields=[
        field(
            "thread_count",
            Int,
            added_in=1,
            default="8",
            short_desc="",
            long_desc=""
        ),
    ],
)
