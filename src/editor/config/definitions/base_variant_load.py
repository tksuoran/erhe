from erhe_codegen import *

# When a glTF load builds a prepared primitive's base vertex variant
# (Load_config::base_variant, doc/plans/gltf_fast_load.md "Item 10").
# platform_default resolves to at_load on desktop and on_demand in OpenXR
# (resolved by get_base_variant_load() in parsers/gltf.cpp).
enum("Base_variant_load",
    value("platform_default", 0, short_desc="At load on desktop, on demand in OpenXR"),
    value("at_load",          1, short_desc="Build and upload the base variant at load"),
    value("on_demand",        2, short_desc="Draw from the optimized variant; build the base variant when a feature needs it"),
    underlying_type=UInt,
)
