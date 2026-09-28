from erhe_codegen import *

# Face culling used while rendering shadow casters into the shadow map.
# Back culling (the default) keeps front faces: the lit face of a receiver
# is in the map and the receiver's minimum bias resolves that tie, and
# single-sided geometry casts shadows from the side facing the light. Front
# culling keeps only back faces, which leak light where a caster touches a
# receiver (its back face meets or is coplanar with the receiver at the
# contact). None rasterizes both sides; on closed meshes it stores what back
# culling stores, rasterizing twice the faces (doc/erhe/shadows.md "Shadow
# pass mechanics"). Selects the
# Shadow_renderer caster pipeline; keep in sync with Shadow_cull_mode in
# src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.hpp.
enum("Shadow_cull_mode",
    value("cull_front", 0, short_desc="Cull front faces (back faces cast shadow)"),
    value("cull_back",  1, short_desc="Cull back faces (front faces cast shadow)"),
    value("cull_none",  2, short_desc="No culling (both faces cast shadow)"),
    underlying_type=UInt,
)
