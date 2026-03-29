from erhe_codegen import *

struct("Icon_settings_config",
    version=1,
    short_desc="Icon size settings",
    long_desc="",
    developer=False,
    fields=[
        field("small_icon_size",  Int, added_in=1, default="16"),
        field("large_icon_size",  Int, added_in=1, default="32"),
        field("hotbar_icon_size", Int, added_in=1, default="128"),
    ],
)
