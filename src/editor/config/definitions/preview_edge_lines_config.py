from erhe_codegen import *

# Edge-line overlay for the offscreen preview thumbnails (geometry graph node
# previews and brush previews, both rendered by Brush_preview). When enabled,
# the preview composer draws a second solid-wireframe pass (expanded fill soup
# + SOLID_WIREFRAME shader variant, depth less-or-equal overlay) on top of the
# polygon fill, so real polygon edges are visible with no z-fight. Requires
# Device_info::use_solid_wireframe (where unsupported, e.g. macOS OpenGL 4.1,
# the toggle is inert). Two instances live in Editor_settings_config:
# graph_node_preview_edge_lines and brush_preview_edge_lines.
struct("Preview_edge_lines_config",
    version=1,
    short_desc="Preview Edge Lines",
    long_desc="Edge-line overlay for preview thumbnails (solid-wireframe pass over the polygon fill)",
    developer=False,
    fields=[
        field("enabled", Bool,  added_in=1, default="true", short_desc="Enabled", long_desc="Draw polygon edge lines on top of the fill in the preview."),
        field("width",   Float, added_in=1, default="1.5f", short_desc="Width",   long_desc="Edge line width in pixels."),
        field("color",   Vec4,  added_in=1, default="0.0f, 0.0f, 0.0f, 1.0f", short_desc="Color", long_desc="Edge line color."),
    ],
)
