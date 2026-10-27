from erhe_codegen import *

# How the standard shader loops the shaded lights (doc/plans/clustered_lights.md
# C1, C5); the value is erhe::scene_renderer::Light_shading.
#   loops     = per light type loops whose bounds are shader variant axes; the
#               preset's per type unshadowed counts decide how many lights
#               are shaded.
#   clustered = every active light is shaded (up to the renderer's
#               max_light_count); spot and point lights are looped per
#               fragment from the fragment's light cluster list.
#   automatic = clustered while the scene has more than
#               erhe::scene_renderer::automatic_clustered_light_count (16)
#               active lights, else loops; decided when the scene's light
#               set is resolved after a light change. The default.
enum("Light_shading_mode",
    value("loops",     0, short_desc="Per type loops (preset light limits)"),
    value("clustered", 1, short_desc="Clustered (every light shaded)"),
    value("automatic", 2, short_desc="Automatic (clustered above 16 lights)"),
    underlying_type=UInt,
)
