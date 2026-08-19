from erhe_codegen import *

struct("Load_config",
    reflect=True,
    version=2,
    short_desc="Loading",
    long_desc="glTF import/open performance options.",
    developer=False,
    fields=[
        field(
            "deferred_raytrace",
            Bool,
            added_in=1,
            default="true",
            short_desc="Deferred Raytrace",
            long_desc="Build an axis-aligned bounding box proxy raytrace for each imported mesh at load time (picking works immediately, on approximate bounds) and build the exact triangle raytrace in background tasks after the glTF file has finished loading. Disable to build the full triangle raytrace synchronously during load.",
            visible=True,
            developer=False
        ),
        field(
            "deferred_edge_lines",
            Bool,
            added_in=1,
            default="true",
            short_desc="Deferred Edge Lines",
            long_desc="Imported meshes are first shown with fill triangles only; geometry edges, wireframe lines and corner/centroid points are built in background tasks after the glTF file has finished loading. Disable to build everything synchronously during load.",
            visible=True,
            developer=False
        ),
        field(
            "parallel_gltf_parse",
            Bool,
            added_in=1,
            default="true",
            short_desc="Parallel glTF Parse",
            long_desc="Decode images, parse meshes and parse animations of a glTF file in parallel worker tasks. Disable to run every glTF loading step serially on the loading thread.",
            visible=True,
            developer=False
        ),
        # Asynchronous loading (doc/async-asset-loading-plan.md). The master
        # switch selects between the blocking load path (whole glTF inside one
        # tick) and Asset_load_tasks advanced a bounded amount from
        # Asset_manager::tick. The remaining fields are that bound: they are
        # the per-frame budget an Asset_load_tick_context hands to the tasks.
        # load_time_slice_ms is checked between items and WINS over the byte
        # budgets, which are the upper bound within that slice.
        field(
            "async_gltf_load",
            Bool,
            added_in=2,
            default="true",
            short_desc="Asynchronous glTF Load",
            long_desc="Load glTF files as tasks advanced a bounded amount every frame, so the editor keeps ticking, stays interactive and keeps presenting frames while a scene streams in. Disable to load the whole file inside the tick that starts the load.",
            visible=True,
            developer=False
        ),
        field(
            "gpu_upload_bytes_per_frame",
            Int,
            added_in=2,
            default="4 * 1024 * 1024",
            short_desc="GPU Upload Bytes / Frame",
            long_desc="Upper bound on the texture and vertex/index bytes an asynchronous load records into the frame command buffer in one frame.",
            ui_min="65536",
            ui_max="67108864",
            hard_min="65536",
            visible=True,
            developer=True
        ),
        field(
            "io_read_bytes_per_frame",
            Int,
            added_in=2,
            default="64 * 1024 * 1024",
            short_desc="File Read Bytes / Frame",
            long_desc="Upper bound on the file bytes an asynchronous load pulls from disk in one frame.",
            ui_min="1048576",
            ui_max="536870912",
            hard_min="65536",
            visible=True,
            developer=True
        ),
        field(
            "max_decoded_bytes_in_flight",
            Int,
            added_in=2,
            default="128 * 1024 * 1024",
            short_desc="Max Decoded Bytes In Flight",
            long_desc="Backpressure: an asynchronous load stops scheduling image decode and mesh build work while this many decoded bytes are already waiting for GPU upload budget. This is what keeps loading memory bounded.",
            ui_min="16777216",
            ui_max="1073741824",
            hard_min="1048576",
            visible=True,
            developer=True
        ),
        field(
            "max_residency_items_per_frame",
            Int,
            added_in=2,
            default="64",
            short_desc="Max Residency Items / Frame",
            long_desc="Upper bound on the number of items (textures, meshes) an asynchronous load makes resident in one frame, so that many tiny items cannot overrun the frame under the byte budget alone.",
            ui_min="1",
            ui_max="4096",
            hard_min="1",
            visible=True,
            developer=True
        ),
        field(
            "max_publish_items_per_frame",
            Int,
            added_in=2,
            default="256",
            short_desc="Max Publish Items / Frame",
            long_desc="Slice size for inserting a loaded node tree into the scene. Publish is atomic: if a tree exceeds the slice the slice loses and publish overruns the frame.",
            ui_min="1",
            ui_max="65536",
            hard_min="1",
            visible=True,
            developer=True
        ),
        field(
            "load_time_slice_ms",
            Float,
            added_in=2,
            default="4.0f",
            short_desc="Load Time Slice (ms)",
            long_desc="Hard cap on the time asynchronous loading spends on the main thread per frame. Checked between items and wins over the byte budgets.",
            ui_min="0.5f",
            ui_max="33.0f",
            hard_min="0.1f",
            visible=True,
            developer=True
        ),
    ],
)
