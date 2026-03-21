from erhe_codegen import *

struct("Mesh_memory_config",
    field(
        "vertex_buffer_size",
        Int,
        added_in=1,
        default="128",
        short_desc="",
        long_desc=""
    ),
    field(
        "index_buffer_size",
        Int,
        added_in=1,
        default="64",
        short_desc="",
        long_desc=""
    ),
    version=1,
)
