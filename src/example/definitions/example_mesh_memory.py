from erhe_codegen import *

struct("Example_mesh_memory",
    version=1,
    short_desc="Example app mesh memory settings",
    long_desc="",
    developer=False,
    fields=[
        field("vertex_buffer_size", Int, added_in=1, default="32", short_desc="Vertex Buffer (MB)"),
        field("index_buffer_size",  Int, added_in=1, default="8",  short_desc="Index Buffer (MB)"),
    ],
)
