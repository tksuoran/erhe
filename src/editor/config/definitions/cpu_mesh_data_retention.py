from erhe_codegen import *

# What a glTF load does with a primitive's CPU triangle soup once its GPU
# buffers and raytrace are built (Load_config::cpu_mesh_data,
# doc/editor/async_asset_loading.md "Releasing CPU mesh data").
# platform_default resolves to keep on desktop and release in OpenXR
# (resolved by get_cpu_mesh_data_retention() in parsers/gltf.cpp).
enum("Cpu_mesh_data_retention",
    value("platform_default", 0, short_desc="Keep on desktop, release in OpenXR"),
    value("keep",             1, short_desc="Keep the CPU triangle soups in memory"),
    value("release",          2, short_desc="Release the CPU triangle soups after upload, reload from the file on demand"),
    underlying_type=UInt,
)
