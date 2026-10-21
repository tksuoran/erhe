from erhe_codegen import *

# How the standard shader loops the shaded lights (doc/plans/clustered_lights.md
# C1, C5); the value is erhe::scene_renderer::Light_shading.
#   loops     = per light type loops whose bounds are shader variant axes; the
#               preset's per type unshadowed counts decide how many lights
#               are shaded. The default.
#   clustered = every active light is shaded (up to the renderer's
#               max_light_count); spot and point lights are looped per
#               fragment from the fragment's light cluster list.
enum("Light_shading_mode",
    value("loops",     0, short_desc="Per type loops (preset light limits)"),
    value("clustered", 1, short_desc="Clustered (every light shaded)"),
    underlying_type=UInt,
)
