from erhe_codegen import *

struct("Editor_settings_config",
    version=3,
    short_desc="Editor settings",
    long_desc="Runtime-editable settings saved to editor_settings.json.",
    developer=False,
    fields=[
        field("camera_controls",      StructRef("Camera_controls_config"), added_in=1),
        # Editor-global appearance of the debug visualizations (colors, line
        # widths, label geometry), shared by all scene views. The per-view
        # enable toggles / visualization modes (Debug_visualizations_settings)
        # live ONLY per scene view, in Scene_view_settings below - there is no
        # editor-global slot for them. New views fall back to the
        # Debug_visualizations_settings struct defaults.
        field("debug_visualizations_style", StructRef("Debug_visualizations_style"), added_in=1),
        # Editor-global render-style appearance (colors, line / point widths,
        # point size, per-primitive color sources), shared by all scene views.
        # Two sets: render_style_appearance for the Default style (non-selected
        # meshes) and selected_render_style_appearance for the Selection style.
        # The per-view render styles (Viewport_config.render_style_*) keep only
        # the visibility toggles (Render_style_data).
        field("render_style_appearance",          StructRef("Render_style_appearance"), added_in=1),
        field("selected_render_style_appearance", StructRef("Render_style_appearance"), added_in=1),
        # Editor-global selection outline appearance, shared by all scene views.
        field("selection_outline",                StructRef("Selection_outline_style"), added_in=1),
        # Editor-global mesh-component (vertex / edge / face) selection style,
        # shared by all scene views.
        field("mesh_component_style",             StructRef("Mesh_component_style"),    added_in=1),
        # Editor-global viewport clear color, shared by all scene views (moved
        # out of the per-view Visual Style popup). gizmo_scale moved on to
        # Transform_tool_config in v17.
        field("clear_color",                      Vec4,  added_in=1, default="0.0f, 0.0f, 0.0f, 0.4f", short_desc="Clear Color", long_desc="Viewport background clear color."),
        # Editor-global content edge-line (wide-line) method and bias tuning.
        field("content_edge_lines",               StructRef("Content_edge_lines_config"), added_in=1),
        # Edge-line overlay for the preview thumbnails: geometry graph node
        # previews (on by default) and brush previews (off by default; the
        # designated-initializer default overrides only 'enabled', the other
        # members keep the struct's own defaults).
        field("graph_node_preview_edge_lines",    StructRef("Preview_edge_lines_config"), added_in=1, short_desc="Graph Node Preview Edge Lines"),
        field("brush_preview_edge_lines",         StructRef("Preview_edge_lines_config"), added_in=1, short_desc="Brush Preview Edge Lines", default=".enabled = false"),
        # Editor-global geometry graph node preview thumbnails: visibility
        # (on by default) and hover auto-rotation.
        field("graph_node_previews",              StructRef("Graph_node_previews_config"), added_in=1),
        field("scene_views",          Vector(StructRef("Scene_view_settings")), added_in=1),
        field("developer",            StructRef("Developer_config"),       added_in=1),
        field("grid",                 StructRef("Grid_config"),            added_in=1),
        field("headset",              StructRef("Headset_config"),         added_in=1),
        field("hotbar",               StructRef("Hotbar_config"),          added_in=1),
        field("hud",                  StructRef("Hud_config"),             added_in=1),
        field("id_renderer",          StructRef("Id_renderer_config"),     added_in=1),
        field("ray_trace",            StructRef("Ray_trace_config"),       added_in=1),
        field("ddgi",                 StructRef("Ddgi_config"),            added_in=1),
        field("lightmap",             StructRef("Lightmap_config"),        added_in=1),
        field("inventory",            StructRef("Inventory_config"),       added_in=1),
        # glTF import/open performance options (doc/gltf-load-speedup-plan.md).
        field("load",                 StructRef("Load_config"),            added_in=1),
        field("network",              StructRef("Network_config"),         added_in=1),
        field("physics",              StructRef("Physics_config"),         added_in=1),
        field("scene",                StructRef("Scene_config"),           added_in=1),
        field("shadow_frustum_fit",   StructRef("Shadow_frustum_fit_config"), added_in=1),
        field("sky",                  StructRef("Sky_config"),             added_in=1),
        field("thumbnails",           StructRef("Thumbnails_config"),      added_in=1),
        field("transform_tool",       StructRef("Transform_tool_config"), added_in=1),
        field("viewport",             StructRef("Viewport_config_data"),  added_in=1),
        field("graphics_preset_name", String, added_in=1, default='"Medium"'),
        field("imgui",                StructRef("Imgui_settings_config"), added_in=1),
        field("icons",                StructRef("Icon_settings_config"),  added_in=1),
        field("threading",            StructRef("Threading_config"),     added_in=1),
        field(
            "post_processing",
            Bool,
            added_in=1,
            default="true",
            short_desc="Post Processing",
            long_desc="Enable Post Processing",
            visible=True,
            developer=False
        ),
        # doc/draw_list_renderer_requirements.md: render eligible composition
        # passes and shadow maps from the scene's persistent draw lists
        # (Draw_list_scene) instead of re-bucketing mesh spans every pass.
        # Runtime toggle; ineligible passes always use the classic path.
        field(
            "use_draw_lists",
            Bool,
            added_in=2,
            default="false",
            short_desc="Draw Lists",
            long_desc="Render content fill and shadow maps through persistent per-scene draw lists (faster with many primitives). Off = classic per-pass bucketing.",
            visible=True,
            developer=False
        ),
        # Unlit (KHR_materials_unlit) primitives are typically sky domes,
        # backdrops and emissive decals: geometry that should not occlude the
        # lit scene and should not drag the camera framing out to the horizon.
        # They still count toward the camera far plane, so the backdrop stays
        # visible.
        field(
            "exclude_unlit_primitives",
            Bool,
            added_in=3,
            default="true",
            short_desc="Exclude Unlit Primitives",
            long_desc="Unlit (KHR_materials_unlit) primitives do not cast shadows and are ignored when framing the camera on scene open. They still count toward the camera far plane.",
            visible=True,
            developer=False
        ),
        field(
            "geometry_edit_mode",
            EnumRef("Geometry_edit_mode"),
            added_in=1,
            default="Geometry_edit_mode::shared",
            short_desc="Geometry Edit Mode",
            long_desc="When editing components of a mesh whose geometry is shared by other meshes (e.g. a duplicate), whether to edit the shared geometry (all instances change) or fork a copy on edit (only this instance changes).",
            visible=True,
            developer=False
        ),
        field(
            "transform_mode",
            EnumRef("Mesh_transform_mode"),
            added_in=1,
            default="Mesh_transform_mode::move",
            short_desc="Mesh Transform Mode",
            long_desc="When transforming a mesh component selection with the gizmo, whether to move the selected components or extrude them (duplicate the selection boundary and bridge it with new faces) before moving.",
            visible=True,
            developer=False
        ),
        field(
            "quit_after_frames",
            Int,
            added_in=1,
            default="0",
            short_desc="Quit After Frames",
            long_desc="Exit editor after this many frames. 0 to disable.",
            visible=True,
            developer=True
        ),
    ],
)
