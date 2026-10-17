from erhe_codegen import *

# Where the draw lists' culling classification runs
# (doc/plans/culling.md "Two complete paths"): the CPU lane loops, or a
# compute pass on the GPU as well. Mirrors
# erhe::scene_renderer::Cull_classification. The CPU masks drive the draws
# either way until GPU command generation consumes the GPU masks.
enum("Culling_classification",
    value("cpu", 0, short_desc="CPU"),
    value("gpu", 1, short_desc="GPU (compute; needs native multi-draw indirect and first-instance record indexing)"),
    underlying_type=UInt,
)
