from erhe_codegen import *

# Per-scene-view Visual Style shadow mode (Viewport_config::shadow_mode).
#   no_shadows      = shadow-map rendering and sampling disabled for the view;
#                     every light shades unshadowed.
#   shadow_maps     = live shadow maps (the default): the view's
#                     Shadow_render_node renders and the forward pass samples.
#   baked_lightmaps = render with the baked lightmap piece meshes
#                     (Lightmap_partitioner render proxies); shadow-map updates
#                     are disabled exactly as in no_shadows. Mirrored by the
#                     Lightmap window's "Render with lightmaps" checkbox.
enum("Shadow_mode",
    value("no_shadows",      0, short_desc="No Shadows"),
    value("shadow_maps",     1, short_desc="Shadow Maps"),
    value("baked_lightmaps", 2, short_desc="Baked Lightmaps"),
    underlying_type=UInt,
)
