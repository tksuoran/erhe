from erhe_codegen import *

# Per-Scene_view persisted settings, keyed by the view's stable name
# ("Default Viewport", "Headset", ...). Editor_settings_config stores these
# in a vector; each Scene_view constructed with an Editor_settings_store and
# a non-empty settings key reads its entry by name at construction and
# updates (or appends) it through a registered collect callback. Views
# without a saved entry start from the global debug_visualizations slot in
# Editor_settings_config, which acts as the defaults for new views.
#
# This is the single per-viewport settings struct: it hosts the per-view
# Visual Style (viewport_config), the Scene and Camera selection
# (scene_and_camera), and the Debug Visualizations state. The defaults for
# viewport_config come from config/editor/default_viewport_config.json (see
# make_viewport_config()); a saved entry overrides them per view.
struct("Scene_view_settings",
    version=2,
    short_desc="Scene View Settings",
    long_desc="Per scene view persisted settings, keyed by view name",
    developer=False,
    fields=[
        field("name",                 String, added_in=1, default='""', short_desc="Name"),
        field("debug_visualizations", StructRef("Debug_visualizations_settings"), added_in=1, short_desc="Debug Visualizations"),
        field("viewport_config",      StructRef("Viewport_config"),              added_in=2, short_desc="Visual Style"),
        field("scene_and_camera",     StructRef("Scene_and_camera_settings"),    added_in=2, short_desc="Scene and Camera"),
    ],
)
