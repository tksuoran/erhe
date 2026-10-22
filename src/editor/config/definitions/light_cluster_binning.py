from erhe_codegen import *

# Where the light cluster lists of a clustered view are binned
# (doc/plans/clustered_lights.md C4): the CPU binner, or a compute pass on the
# GPU with the CPU binner as the reference of get_light_cluster_check.
# Mirrors erhe::scene_renderer::Light_cluster_binning.
enum("Light_cluster_binning",
    value("cpu", 0, short_desc="CPU"),
    value("gpu", 1, short_desc="GPU (compute)"),
    underlying_type=UInt,
)
