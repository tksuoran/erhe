from erhe_codegen import *

struct("Renderdoc_config",
    field("capture_support", Bool, added_in=1, default="false"),
    version=1,
)
