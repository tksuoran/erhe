from erhe_codegen import *

struct("Windows_visibility_config",
    version=1,
    short_desc="Window visibility state",
    long_desc="",
    developer=False,
    fields=[
        field("windows", Vector(StructRef("Window_visibility_entry")), added_in=1),
    ],
)
