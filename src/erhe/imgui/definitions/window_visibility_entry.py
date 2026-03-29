from erhe_codegen import *

struct("Window_visibility_entry",
    version=1,
    short_desc="Window visibility entry",
    long_desc="",
    developer=False,
    fields=[
        field("label",   String, added_in=1, default='""'),
        field("visible", Bool,   added_in=1, default="true"),
    ],
)
