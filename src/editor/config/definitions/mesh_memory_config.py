from erhe_codegen import *

struct("Mesh_memory_config",
    version=1,
    short_desc="Renderer Mesh Memory",
    long_desc="",
    developer=False,
    fields=[
        field(
            "vertex_buffer_size",
            Int,
            added_in=1,
            default="128",
            short_desc="Vertex Buffer Memory (MB)",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "index_buffer_size",
            Int,
            added_in=1,
            default="64",
            short_desc="Index Buffer Memory (MB)",
            long_desc="",
            visible=True,
            developer=False
        ),
    ],
)
