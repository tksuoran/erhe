from erhe_codegen import *

struct("Logging_config",
    version=1,
    short_desc="Logging configuration",
    long_desc="",
    developer=False,
    fields=[
        field("loggers", Vector(StructRef("Logger_entry")), added_in=1),
    ],
)
