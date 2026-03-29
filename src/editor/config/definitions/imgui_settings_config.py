from erhe_codegen import *

struct("Imgui_settings_config",
    version=1,
    short_desc="ImGui font and UI settings",
    long_desc="",
    developer=False,
    fields=[
        field("primary_font",              String, added_in=1, default='"res/fonts/SourceSansPro-Regular.otf"'),
        field("mono_font",                 String, added_in=1, default='"res/fonts/SourceCodePro-Semibold.otf"'),
        field("font_size",                 Float,  added_in=1, default="16.0f"),
        field("vr_font_size",              Float,  added_in=1, default="24.0f"),
        field("material_design_font_size", Float,  added_in=1, default="16.0f"),
        field("icon_font_size",            Float,  added_in=1, default="16.0f"),
    ],
)
