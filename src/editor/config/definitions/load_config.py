from erhe_codegen import *

struct("Load_config",
    version=1,
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
    ],
)
