from erhe_codegen import *

struct("Mesh_memory_config",
    version=1,
    short_desc="Mesh Memory",
    long_desc="",
    developer=False,
    fields=[
        # Deprecated since the introduction of lazy buffer pools. Kept so that
        # existing JSON configs still parse. The pool block size is now
        # controlled by vertex_pool_block_size_mb / index_pool_block_size_mb.
        field(
            "vertex_buffer_size",
            Int,
            added_in=1,
            default="128",
            short_desc="Vertex Buffer Memory (MB) (deprecated)",
            long_desc="Deprecated. Use vertex_pool_block_size_mb instead.",
            visible=False,
            developer=False
        ),
        field(
            "index_buffer_size",
            Int,
            added_in=1,
            default="64",
            short_desc="Index Buffer Memory (MB) (deprecated)",
            long_desc="Deprecated. Use index_pool_block_size_mb instead.",
            visible=False,
            developer=False
        ),
        # Lazy pool sizing: each pool starts empty and grows by appending a new
        # Buffer block of these sizes when an allocation request would not fit
        # any existing block. Allocations larger than the block size get a
        # buffer sized to fit.
        field(
            "vertex_pool_block_size_mb",
            Int,
            added_in=1,
            default="32",
            short_desc="Vertex Pool Block Size (MB)",
            long_desc="Default size of a freshly grown vertex buffer block in any format pool.",
            visible=True,
            developer=False
        ),
        field(
            "index_pool_block_size_mb",
            Int,
            added_in=1,
            default="16",
            short_desc="Index Pool Block Size (MB)",
            long_desc="Default size of a freshly grown index buffer block.",
            visible=True,
            developer=False
        ),
        field(
            "edge_line_vertex_pool_block_size_mb",
            Int,
            added_in=1,
            default="8",
            short_desc="Edge Line Vertex Pool Block Size (MB)",
            long_desc="Default size of a freshly grown edge-line vertex buffer block.",
            visible=True,
            developer=False
        ),
        field(
            "max_buffers_per_pool",
            Int,
            added_in=1,
            default="64",
            short_desc="Max Buffers per Pool",
            long_desc="Soft cap on the number of GPU buffers any one pool may grow to. Allocation requests that would exceed this fail loud.",
            visible=True,
            developer=True
        ),
    ],
)
