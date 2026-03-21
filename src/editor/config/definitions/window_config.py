from erhe_codegen import *

struct("Window_config",
    field("show",              Bool,  added_in=1, default="true"),
    field("fullscreen",        Bool,  added_in=1, default="false"),
    field("use_transparency",  Bool,  added_in=1, default="false"),
    field("gl_major",          Int,   added_in=1, default="4"),
    field("gl_minor",          Int,   added_in=1, default="6"),
    field("size",              IVec2, added_in=1, default="1920, 1080"),
    field("color_bit_depth",   Int,   added_in=1, default="8"),
    field("high_pixel_density", Bool, added_in=1, default="true"),
    field("use_finish",        Bool,  added_in=1, default="false"),
    field("use_sleep",         Bool,  added_in=1, default="false"),
    field("sleep_margin",      Float, added_in=1, default="0.0f"),
    field("swap_interval",     Int,   added_in=1, default="0"),
    field("enable_joystick",   Bool,  added_in=1, default="true"),
    version=1,
)
