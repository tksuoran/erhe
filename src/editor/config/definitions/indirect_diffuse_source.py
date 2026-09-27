from erhe_codegen import *

# The producer of the runtime indirect diffuse probe field
# (doc/editor/radiance_cascades.md "Source selection"). DDGI and radiance
# cascades write the same probe field format the forward pass samples, so
# at most one of them is active; 'ambient' keeps the flat scene ambient
# term. Replaces Ddgi_config::enabled (removed in Ddgi_config v2; the
# Editor_settings_config v5 migration maps enabled = true to 'ddgi').
enum("Indirect_diffuse_source",
    value("ambient",           0, short_desc="Ambient (flat scene ambient term)"),
    value("ddgi",              1, short_desc="DDGI (dynamic diffuse global illumination)"),
    value("radiance_cascades", 2, short_desc="Radiance cascades"),
    underlying_type=UInt,
)
