from erhe_codegen import *

struct("Viewport_config_data",
    field("gizmo_scale",              Float, added_in=1, default="3.0f"),
    field("polygon_fill",            Bool,  added_in=1, default="true"),
    field("edge_lines",              Bool,  added_in=1, default="false"),
    field("selection_polygon_fill",  Bool,  added_in=1, default="true"),
    field("selection_edge_lines",    Bool,  added_in=1, default="false"),
    field("corner_points",           Bool,  added_in=1, default="false"),
    field("polygon_centroids",       Bool,  added_in=1, default="false"),
    field("selection_bounding_box",  Bool,  added_in=1, default="true"),
    field("selection_bounding_sphere", Bool, added_in=1, default="true"),
    field("edge_color",              Vec4,  added_in=1, default="0.0f, 0.0f, 0.0f, 0.5f"),
    field("selection_edge_color",    Vec4,  added_in=1, default="0.0f, 0.0f, 0.0f, 0.5f"),
    field("clear_color",             Vec4,  added_in=1, default="0.1f, 0.2f, 0.4f, 1.0f"),
    version=1,
)
