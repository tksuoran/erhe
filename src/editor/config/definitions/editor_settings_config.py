from erhe_codegen import *

struct("Editor_settings_config",
    version=8,
    short_desc="Editor settings",
    long_desc="Runtime-editable settings saved to editor_settings.json.",
    developer=False,
    fields=[
        field("camera_controls",      StructRef("Camera_controls_config"), added_in=1),
        # Defaults for new scene views: a view with no entry in scene_views
        # reads this slot at construction. Per-view state is persisted in
        # scene_views below.
        field("debug_visualizations", StructRef("Debug_visualizations_settings"), added_in=2),
        # Editor-global appearance of the debug visualizations (colors, line
        # widths, label geometry), shared by all scene views.
        field("debug_visualizations_style", StructRef("Debug_visualizations_style"), added_in=8),
        field("scene_views",          Vector(StructRef("Scene_view_settings")), added_in=5),
        field("developer",            StructRef("Developer_config"),       added_in=1),
        field("grid",                 StructRef("Grid_config"),            added_in=1),
        field("headset",              StructRef("Headset_config"),         added_in=1),
        field("hotbar",               StructRef("Hotbar_config"),          added_in=1),
        field("hud",                  StructRef("Hud_config"),             added_in=1),
        field("id_renderer",          StructRef("Id_renderer_config"),     added_in=1),
        field("inventory",            StructRef("Inventory_config"),       added_in=1),
        field("network",              StructRef("Network_config"),         added_in=1),
        field("physics",              StructRef("Physics_config"),         added_in=1),
        field("scene",                StructRef("Scene_config"),           added_in=1),
        field("shadow_frustum_fit",   StructRef("Shadow_frustum_fit_config"), added_in=4),
        field("sky",                  StructRef("Sky_config"),             added_in=3),
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
        field(
            "geometry_edit_mode",
            EnumRef("Geometry_edit_mode"),
            added_in=6,
            default="Geometry_edit_mode::shared",
            short_desc="Geometry Edit Mode",
            long_desc="When editing components of a mesh whose geometry is shared by other meshes (e.g. a duplicate), whether to edit the shared geometry (all instances change) or fork a copy on edit (only this instance changes).",
            visible=True,
            developer=False
        ),
        field(
            "transform_mode",
            EnumRef("Mesh_transform_mode"),
            added_in=7,
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
