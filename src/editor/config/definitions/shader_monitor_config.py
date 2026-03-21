from erhe_codegen import *

struct("Shader_monitor_config",
    field("enabled", Bool, added_in=1, default="true"),
    version=1,
)
