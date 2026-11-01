from erhe_codegen import *

# Where the CPU's consumers of the selected levels of detail read them
# (doc/plans/lod_gpu_selection.md section 9, decisions 5 and 6;
# doc/editor/lod.md "Graphics preset settings"). Mirrors
# erhe::scene_renderer::Lod_cpu_selection value for value.
enum("Lod_cpu_selection",
    value("every_frame", 0, short_desc="Every frame (CPU lanes select every frame)"),
    value("on_demand",   1, short_desc="On demand (GPU classification only; CPU consumers read the newest completed frame)"),
    underlying_type=UInt,
)
