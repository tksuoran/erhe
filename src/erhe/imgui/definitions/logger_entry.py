from erhe_codegen import *

struct("Logger_entry",
    version=1,
    short_desc="Logger name and level",
    long_desc="",
    developer=False,
    fields=[
        field("name",  String, added_in=1, default='""'),
        field("level", String, added_in=1, default='"err"'),
    ],
)
