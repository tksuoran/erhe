from erhe_codegen import *

# Per-Scene_view render style: ONLY which primitives are drawn (visibility
# toggles). The appearance of those primitives (colors, line/point widths,
# point size, per-primitive color sources) is editor-global and lives in
# Render_style_appearance (a Default-style set and a Selection-style set in
# Editor_settings_config). Used twice in Viewport_config
# (render_style_not_selected, render_style_selected).
struct("Render_style_data",
    version=2,
    short_desc="Render Style",
    long_desc="Which primitives are drawn for meshes in a viewport",
    developer=False,
    fields=[
        field("polygon_fill",      Bool, added_in=1, default="true",  short_desc="Polygon Fill"),
        field("edge_lines",        Bool, added_in=1, default="false", short_desc="Edge Lines"),
        # Solid wireframe: real polygon edges drawn inside the fill fragment
        # shader (expanded fill mesh), so the wireframe shares the fill's depth
        # and never z-fights. Replaces the normal polygon fill when enabled.
        field("solid_wireframe",   Bool, added_in=2, default="false", short_desc="Solid Wireframe"),
        field("polygon_centroids", Bool, added_in=1, default="false", short_desc="Polygon Centroids"),
        field("corner_points",     Bool, added_in=1, default="false", short_desc="Corner Points"),
    ],
)
