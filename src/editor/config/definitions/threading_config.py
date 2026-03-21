from erhe_codegen import *

struct("Threading_config",
    field("thread_count", Int, added_in=1, default="8"),
    version=1,
)
