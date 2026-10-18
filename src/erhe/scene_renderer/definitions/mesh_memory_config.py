from erhe_codegen import *

struct("Mesh_memory_config",
    reflect=True,
    version=3,
    short_desc="Mesh Memory",
    long_desc="",
    developer=False,
    fields=[
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
            "quantize_vertex_positions",
            Bool,
            added_in=2,
            default="false",
            short_desc="Quantize Vertex Positions",
            long_desc="Store the optimized mesh variant's vertex positions as snorm16x3 normalized into the primitive AABB (6 bytes instead of 12). Applies to the optimized variant only, so it has no effect unless Optimize Meshes is enabled; the base variant always stores full-float positions. Ignored on a device that cannot use snorm16x3 as vertex input.",
            visible=True,
            developer=True
        ),
        field(
            "optimize_meshes",
            Bool,
            added_in=3,
            default="false",
            short_desc="Optimize Meshes",
            long_desc="Build an additional meshoptimizer-optimized variant of every renderable mesh (vertex weld, vertex cache order, overdraw order, vertex fetch order). The unoptimized variant is always built and stays the one the ID renderer, ray tracing and vertex editing use. Applies to meshes built after the change.",
            visible=True,
            developer=True
        ),
        field(
            "mesh_optimize_cache",
            Bool,
            added_in=3,
            default="false",
            short_desc="Mesh Optimization Cache",
            long_desc="Cache mesh optimization results on disk (cache/mesh_optimizer), keyed by source content hash, so repeated loads of the same asset skip the optimization passes. Applies to imported meshes: their optimization at load and the one in the deferred finalize that follows; procedural and edited geometry is not cached. No effect unless Optimize Meshes is enabled.",
            visible=True,
            developer=True
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
