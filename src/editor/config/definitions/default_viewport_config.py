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
        # Selection_outline_style; clear color and gizmo scale moved to the
        # editor-global Editor_settings_config (clear_color / gizmo_scale).
        field("debug_visualizations",          StructRef("Debug_visualizations_config"), added_in=1, short_desc="Debug Visualizations"),
        # Mesh Component Style moved to the editor-global Editor_settings_config
        # (mesh_component_style); shared by all scene views.
        field("selection_bounding_box",        Bool, added_in=1, default="false", short_desc="Selection Bounding Box"),
        field("selection_bounding_sphere",     Bool, added_in=1, default="false", short_desc="Selection Bounding Sphere"),
        field(
            "shadow_mode",
            EnumRef("Shadow_mode"),
            added_in=1,
            default="Shadow_mode::shadow_maps",
            short_desc="Shadows",
            long_desc="Live per-view shadow mode: No Shadows, Shadow Maps, or Baked Lightmaps. Anything but Shadow Maps skips the view's shadow render passes and shades every light unshadowed; Baked Lightmaps additionally renders the lightmap piece meshes (mirrors the Lightmap window's 'Render with lightmaps'). The shadow map budget (resolution, light count) stays with the graphics preset.",
        ),
    ],
)
