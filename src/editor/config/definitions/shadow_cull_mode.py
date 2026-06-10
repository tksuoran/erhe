from erhe_codegen import *

# Face culling used while rendering shadow casters into the shadow map.
# Front culling (the default) keeps only back faces, which reduces
# peter-panning on closed meshes; back culling keeps front faces, letting
# single-sided geometry cast shadows from the side facing the light; none
# rasterizes both sides at the cost of more self-shadowing acne. Selects the
# Shadow_renderer caster pipeline; keep in sync with Shadow_cull_mode in
# src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.hpp.
enum("Shadow_cull_mode",
    value("cull_front", 0, short_desc="Cull front faces (back faces cast shadow)"),
    value("cull_back",  1, short_desc="Cull back faces (front faces cast shadow)"),
    value("cull_none",  2, short_desc="No culling (both faces cast shadow)"),
    underlying_type=UInt,
)
