from erhe_codegen import *

struct("Viewport_config_data",
    version=1,
    short_desc="Default Viewport",
    long_desc="Fallback default Visual Style for new scene viewports. Used only when config/editor/default_viewport_config.json is absent; once a viewport has its own Visual Style (edited from its toolbar Visual Style popup) that per-view style overrides these values.",
    developer=False,
    fields=[
        field(
            "gizmo_scale",
            Float,
            added_in=1,
            default="3.0f",
            short_desc="Gizmo Scale",
            long_desc="Scale factor applied to the transform gizmo handles in a new viewport.",
            visible=True,
            developer=False
        ),
        field(
            "polygon_fill",
            Bool,
            added_in=1,
            default="true",
            short_desc="Polygon Fill",
            long_desc="Default (non-selected) style: draw solid polygon fill for meshes.",
            visible=True,
            developer=False
        ),
        field(
            "edge_lines",
            Bool,
            added_in=1,
            default="false",
            short_desc="Edge Lines",
            long_desc="Default (non-selected) style: draw mesh edge (wireframe) lines.",
            visible=True,
            developer=False
        ),
        field(
            "selection_polygon_fill",
            Bool,
            added_in=1,
            default="true",
            short_desc="Selection Polygon Fill",
            long_desc="Selected style: draw solid polygon fill for selected meshes.",
            visible=True,
            developer=False
        ),
        field(
            "selection_edge_lines",
            Bool,
            added_in=1,
            default="false",
            short_desc="Selection Edge Lines",
            long_desc="Selected style: draw mesh edge (wireframe) lines for selected meshes.",
            visible=True,
            developer=False
        ),
        field(
            "corner_points",
            Bool,
            added_in=1,
            default="false",
            short_desc="Corner Points",
            long_desc="Draw a point marker at each mesh corner. Applies to both the selected and non-selected styles.",
            visible=True,
            developer=False
        ),
        field(
            "polygon_centroids",
            Bool,
            added_in=1,
            default="false",
            short_desc="Polygon Centroids",
            long_desc="Draw a marker at each polygon centroid. Applies to both the selected and non-selected styles.",
            visible=True,
            developer=False
        ),
        field(
            "selection_bounding_box",
            Bool,
            added_in=1,
            default="true",
            short_desc="Selection Bounding Box",
            long_desc="Draw the axis-aligned bounding box around the current selection.",
            visible=True,
            developer=False
        ),
        field(
            "selection_bounding_sphere",
            Bool,
            added_in=1,
            default="true",
            short_desc="Selection Bounding Sphere",
            long_desc="Draw the bounding sphere around the current selection.",
            visible=True,
            developer=False
        ),
        field(
            "edge_color",
            Vec4,
            added_in=1,
            default="0.0f, 0.0f, 0.0f, 0.5f",
            short_desc="Edge Line Color",
            long_desc="Color of the edge (wireframe) lines for non-selected meshes.",
            visible=True,
            developer=False
        ),
        field(
            "selection_edge_color",
            Vec4,
            added_in=1,
            default="0.0f, 0.0f, 0.0f, 0.5f",
            short_desc="Selection Edge Line Color",
            long_desc="Color of the edge (wireframe) lines for selected meshes.",
            visible=True,
            developer=False
        ),
        field(
            "clear_color",
            Vec4,
            added_in=1,
            default="0.1f, 0.2f, 0.4f, 1.0f",
            short_desc="Clear Color",
            long_desc="Background color the viewport is cleared to before rendering.",
            visible=True,
            developer=False
        ),
    ],
)
