from erhe_codegen import *

# Editor-global content edge-line (wide-line) method + tuning, shared by all
# scene views. Moved out of the per-Scene_view Visual Style popup, which used to
# poke these straight onto the Content_wide_line_renderer as runtime-only state.
# Now persisted here and pushed to the renderer each frame (see
# viewport_scene_view / headset_view, before begin_frame). The simple quad and
# the two-face surface tent each win in different cases (the tent hugs sharp
# creases but can poke through overlapping thin geometry; the simple quad is
# flatter but more predictable), so both are kept and chosen here. Defaults match
# the Content_wide_line_renderer member defaults.
struct("Content_edge_lines_config",
    version=1,
    short_desc="Content Edge Lines",
    long_desc="Editor-global content edge-line (wide-line) method and bias tuning",
    developer=False,
    fields=[
        field("use_tent",         Bool,  added_in=1, default="false",   short_desc="Surface Tent", long_desc="Draw content edge lines as a two-face surface tent instead of the simple flat quad."),
        field("line_bias_margin", Float, added_in=1, default="1024.0f", short_desc="Tent Bias (ULPs)",       long_desc="Surface-line depth-bias headroom in depth ULPs (used when Surface Tent is enabled)."),
        field("line_bias_clamp",  Float, added_in=1, default="2048.0f", short_desc="Tent Bias Clamp (ULPs)", long_desc="Maximum surface-line depth bias in depth ULPs (used when Surface Tent is enabled)."),
    ],
)
