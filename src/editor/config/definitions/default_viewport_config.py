from erhe_codegen import *

struct("Viewport_config",
    version=1,
    short_desc="Default Viewport Configuration",
    long_desc="Default viewport configuration saved to default_viewport_config.json",
    developer=False,
    fields=[
        field("render_style_not_selected",     StructRef("Render_style_data"),          added_in=1, short_desc="Default Style"),
        field("render_style_selected",         StructRef("Render_style_data"),          added_in=1, short_desc="Selection Style"),
        # Selection outline appearance moved to the editor-global
        # Selection_outline_style (Editor_settings_config.selection_outline).
        field("clear_color",                   Vec4, added_in=1, default="0.0f, 0.0f, 0.0f, 0.4f", short_desc="Clear Color"),
        field("gizmo_scale",                   Float, added_in=1, default="4.5f", short_desc="Gizmo Scale"),
        field("debug_visualizations",          StructRef("Debug_visualizations_config"), added_in=1, short_desc="Debug Visualizations"),
        # Mesh Component Style moved to the editor-global Editor_settings_config
        # (mesh_component_style); shared by all scene views.
        field("selection_bounding_box",        Bool, added_in=1, default="false", short_desc="Selection Bounding Box"),
        field("selection_bounding_sphere",     Bool, added_in=1, default="false", short_desc="Selection Bounding Sphere"),
    ],
)
