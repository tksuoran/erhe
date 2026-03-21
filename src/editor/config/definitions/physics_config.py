from erhe_codegen import *

struct("Physics_config",
    field("static_enable",  Bool, added_in=1, default="true"),
    field("dynamic_enable", Bool, added_in=1, default="true"),
    version=1,
)
