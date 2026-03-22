from erhe_codegen import *

struct("Physics_config",
    version=1,
    short_desc="",
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
    ],
)
