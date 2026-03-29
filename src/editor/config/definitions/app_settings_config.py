from erhe_codegen import *

struct("App_settings_config",
    version=1,
    short_desc="Application settings",
    long_desc="",
    developer=False,
    fields=[
        field("graphics_preset_name", String,                             added_in=1, default='"Medium"'),
        field("imgui",                StructRef("Imgui_settings_config"), added_in=1),
        field("icons",                StructRef("Icon_settings_config"),  added_in=1),
    ],
)
