from erhe_codegen import *

struct("Thumbnails_config",
    field("capacity",    Int, added_in=1, default="200"),
    field("size_pixels", Int, added_in=1, default="256"),
    version=1,
)
