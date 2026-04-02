from erhe_codegen import *

struct("Render_style_data",
    version=1,
    short_desc="Render Style",
    long_desc="Rendering style for meshes in viewport",
    developer=False,
    fields=[
        field("polygon_fill",      Bool, added_in=1, default="true",  short_desc="Polygon Fill"),
        field("edge_lines",        Bool, added_in=1, default="false", short_desc="Edge Lines"),
        field("polygon_centroids", Bool, added_in=1, default="false", short_desc="Polygon Centroids"),
        field("corner_points",     Bool, added_in=1, default="false", short_desc="Corner Points"),
        field("point_size",        Float, added_in=1, default="4.0f", short_desc="Point Size"),
        field("line_width",        Float, added_in=1, default="-1.0f", short_desc="Line Width"),
        field("line_color",        Vec4, added_in=1, default="0.0f, 0.0f, 0.0f, 0.5f", short_desc="Line Color"),
        field("corner_color",      Vec4, added_in=1, default="0.0f, 0.0f, 1.0f, 1.0f", short_desc="Corner Color"),
        field("centroid_color",    Vec4, added_in=1, default="0.0f, 0.0f, 1.0f, 1.0f", short_desc="Centroid Color"),
        field("edge_lines_color_source",        EnumRef("Primitive_color_source"), added_in=1, default="Primitive_color_source::constant_color",        short_desc="Edge Lines Color Source"),
        field("polygon_centroids_color_source", EnumRef("Primitive_color_source"), added_in=1, default="Primitive_color_source::mesh_wireframe_color", short_desc="Polygon Centroids Color Source"),
        field("corner_points_color_source",     EnumRef("Primitive_color_source"), added_in=1, default="Primitive_color_source::mesh_wireframe_color", short_desc="Corner Points Color Source"),
    ],
)
