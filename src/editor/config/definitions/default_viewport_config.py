from erhe_codegen import *

struct("Viewport_config",
    version=1,
    short_desc="Default Viewport Configuration",
    long_desc="Default viewport configuration saved to default_viewport_config.json",
    developer=False,
    fields=[
        field("render_style_not_selected",     StructRef("Render_style_data"),          added_in=1, short_desc="Default Style"),
        field("render_style_selected",         StructRef("Render_style_data"),          added_in=1, short_desc="Selection Style"),
        field("selection_highlight_low",       Vec4, added_in=1, default="2.0f, 1.0f, 0.0f, 0.8f", short_desc="Selection Highlight Color Low"),
        field("selection_highlight_high",      Vec4, added_in=1, default="2.0f, 1.0f, 0.0f, 0.8f", short_desc="Selection Highlight Color High"),
        field("selection_highlight_width_low",  Float, added_in=1, default="-3.0f", short_desc="Selection Highlight Width Low"),
        field("selection_highlight_width_high", Float, added_in=1, default="-3.0f", short_desc="Selection Highlight Width High"),
        field("selection_highlight_frequency",  Float, added_in=1, default="1.0f",  short_desc="Selection Highlight Frequency"),
        field("clear_color",                   Vec4, added_in=1, default="0.0f, 0.0f, 0.0f, 0.4f", short_desc="Clear Color"),
        field("gizmo_scale",                   Float, added_in=1, default="4.5f", short_desc="Gizmo Scale"),
        field("debug_visualizations",          StructRef("Debug_visualizations_config"), added_in=1, short_desc="Debug Visualizations"),
        field("selection_bounding_box",        Bool, added_in=1, default="false", short_desc="Selection Bounding Box"),
        field("selection_bounding_sphere",     Bool, added_in=1, default="false", short_desc="Selection Bounding Sphere"),
    ],
)
