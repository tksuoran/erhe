from erhe_codegen import *

struct("Hud_config",
    field("enabled", Bool,  added_in=1, default="false"),
    field("show",    Bool,  added_in=1, default="false"),
    field("locked",  Bool,  added_in=1, default="false"),
    field("width",   Int,   added_in=1, default="1024"),
    field("height",  Int,   added_in=1, default="1024"),
    field("ppm",     Float, added_in=1, default="5000.0f"),
    field("x",       Float, added_in=1, default="-0.09f"),
    field("y",       Float, added_in=1, default="0.0f"),
    field("z",       Float, added_in=1, default="-0.38f"),
    version=1,
)
