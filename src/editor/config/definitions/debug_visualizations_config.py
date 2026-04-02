from erhe_codegen import *

struct("Debug_visualizations_config",
    version=1,
    short_desc="Debug Visualizations",
    long_desc="Debug visualization settings for viewport",
    developer=False,
    fields=[
        field("light",  EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::selected", short_desc="Light"),
        field("camera", EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::selected", short_desc="Camera"),
    ],
)
