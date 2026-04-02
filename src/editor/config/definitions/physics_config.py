from erhe_codegen import *

struct("Physics_config",
    version=2,
    short_desc="Physics",
    long_desc="",
    developer=False,
    fields=[
        field(
            "static_enable",
            Bool,
            added_in=1,
            default="true",
            short_desc="Static Enable",
            long_desc="Changing value required editor restart. If not enabled, dynamic enable is not possible",
            visible=True,
            developer=False
        ),
        field(
            "dynamic_enable",
            Bool,
            added_in=1,
            default="true",
            short_desc="Dynamic Enable",
            long_desc="Value can be changed at runtime",
            visible=True,
            developer=False
        ),
        field(
            "debug_draw",
            Bool,
            added_in=2,
            default="false",
            short_desc="Debug Draw",
            long_desc="Enable physics debug visualization",
            visible=True,
            developer=False
        ),
    ],
)
