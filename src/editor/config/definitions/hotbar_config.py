from erhe_codegen import *

struct("Hotbar_config",
    field("enabled",    Bool,  added_in=1, default="true"),
    field("show",       Bool,  added_in=1, default="true"),
    field("use_radial", Bool,  added_in=1, default="false"),
    field("x",          Float, added_in=1, default="0.0f"),
    field("y",          Float, added_in=1, default="-0.140f"),
    field("z",          Float, added_in=1, default="-0.5f"),
    version=1,
)
