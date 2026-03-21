from erhe_codegen import *

struct("Developer_config",
    field("enable", Bool, added_in=1, default="false"),
    version=1,
)
