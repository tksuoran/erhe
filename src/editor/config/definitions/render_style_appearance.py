from erhe_codegen import *

# Editor-global appearance of a render style: the colors, line / point widths,
# point size and per-primitive color sources used when drawing meshes. Split
# out of the per-Scene_view Render_style_data (which keeps only the visibility
# toggles). Editor_settings_config holds two of these - one for the Default
# style (non-selected meshes) and one for the Selection style - shared by all
# scene views. Read by get_primitive_settings() in renderers/render_style.cpp.
# Negative line / point widths are in screen-space pixels and do not scale by
# distance.
struct("Render_style_appearance",
    version=1,
    short_desc="Render Style Appearance",
    long_desc="Editor-global colors, widths and color sources for a render style",
    developer=False,
    fields=[
        field("line_width",        Float, added_in=1, default="-1.0f", short_desc="Edge Line Width", long_desc="Width of mesh edge (wireframe) lines. Negative is a constant screen-space pixel width."),
        field("line_color",        Vec4,  added_in=1, default="0.0f, 0.0f, 0.0f, 0.5f", short_desc="Edge Line Color", long_desc="Constant color for mesh edge lines (used when Edge Line Color Source is Constant Color)."),
        field("edge_lines_color_source", EnumRef("Primitive_color_source"), added_in=1, default="Primitive_color_source::constant_color", short_desc="Edge Line Color Source", long_desc="Where edge line color comes from: a constant color or a per-primitive source."),
        field("solid_wireframe_width", Float, added_in=1, default="1.5f", short_desc="Solid Wireframe Width", long_desc="Edge width for the solid-wireframe pass (real polygon edges drawn inside the fill shader)."),
        field("solid_wireframe_color", Vec4,  added_in=1, default="0.0f, 0.0f, 0.0f, 1.0f", short_desc="Solid Wireframe Color", long_desc="Edge color for the solid-wireframe pass."),
        field("centroid_color",    Vec4,  added_in=1, default="0.0f, 0.0f, 1.0f, 1.0f", short_desc="Centroid Color", long_desc="Constant color for polygon centroid markers (used when its Color Source is Constant Color)."),
        field("polygon_centroids_color_source", EnumRef("Primitive_color_source"), added_in=1, default="Primitive_color_source::mesh_wireframe_color", short_desc="Centroid Color Source", long_desc="Where polygon centroid marker color comes from."),
        field("corner_color",      Vec4,  added_in=1, default="0.0f, 0.0f, 1.0f, 1.0f", short_desc="Corner Point Color", long_desc="Constant color for mesh corner point markers (used when its Color Source is Constant Color)."),
        field("corner_points_color_source", EnumRef("Primitive_color_source"), added_in=1, default="Primitive_color_source::mesh_wireframe_color", short_desc="Corner Point Color Source", long_desc="Where corner point marker color comes from."),
        field("point_size",        Float, added_in=1, default="4.0f", short_desc="Point Size", long_desc="Size of polygon centroid and corner point markers."),
    ],
)
