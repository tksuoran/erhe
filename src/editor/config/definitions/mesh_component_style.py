from erhe_codegen import *

struct("Mesh_component_style",
    version=1,
    short_desc="Mesh Component Style",
    long_desc="Visual style for mesh component (vertex / edge / face) selection",
    developer=False,
    fields=[
        field("vertex_color",    Vec4,  added_in=1, default="0.95f, 0.65f, 0.10f, 1.00f", short_desc="Vertex Color"),
        field("edge_color",      Vec4,  added_in=1, default="1.00f, 0.55f, 0.05f, 1.00f", short_desc="Edge Color"),
        field("face_color",      Vec4,  added_in=1, default="1.00f, 0.55f, 0.05f, 0.35f", short_desc="Face Color"),
        field("hover_color",     Vec4,  added_in=1, default="1.00f, 1.00f, 1.00f, 0.80f", short_desc="Hover Color"),
        # Negative = constant screen-space pixel width (see Primitive_renderer::get_line_width).
        field("edge_thickness",  Float, added_in=1, default="-3.0f",   short_desc="Edge Thickness"),
        # Vertex handle size as a fraction of the distance to the camera, so the
        # handles keep a roughly constant on-screen size.
        field("vertex_size",     Float, added_in=1, default="0.012f",  short_desc="Vertex Size"),
        # Surface-line depth-bias headroom in depth-buffer resolvable units (ULPs);
        # pushed into Debug_renderer::set_line_bias_margin at render time.
        field("edge_depth_bias", Float, added_in=1, default="1024.0f", short_desc="Edge Depth Bias"),
    ],
)
