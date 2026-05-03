from erhe_codegen import *

enum("Perf_settings_level",
    value("e_unset",          0, short_desc="Don't request a level (runtime default)"),
    value("e_power_savings",  1, short_desc="Power savings"),
    value("e_sustained_low",  2, short_desc="Sustained low"),
    value("e_sustained_high", 3, short_desc="Sustained high"),
    value("e_boost",          4, short_desc="Boost"),
)
