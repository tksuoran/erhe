from erhe_codegen import *

struct("Erhe_config",
    version=1,
    short_desc="Erhe init config",
    long_desc="Startup configuration loaded from erhe.json. Not modified at runtime.",
    developer=False,
    fields=[
        field("developer",      StructRef("Developer_config"),      added_in=1),
        field("graphics",       StructRef("Graphics_config"),       added_in=1),
        field("mesh_memory",    StructRef("Mesh_memory_config"),    added_in=1),
        field("renderer",       StructRef("Renderer_config"),       added_in=1),
        field("shader_monitor", StructRef("Shader_monitor_config"), added_in=1),
        field("text_renderer",  StructRef("Text_renderer_config"),  added_in=1),
        field("threading",      StructRef("Threading_config"),      added_in=1),
        field("window",         StructRef("Window_config"),         added_in=1),
    ],
)
