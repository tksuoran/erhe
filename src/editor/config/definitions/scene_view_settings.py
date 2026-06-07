from erhe_codegen import *

# Per-Scene_view persisted settings, keyed by the view's stable name
# ("Default Viewport", "Headset", ...). Editor_settings_config stores these
# in a vector; each Scene_view constructed with an Editor_settings_store and
# a non-empty settings key reads its entry by name at construction and
# updates (or appends) it through a registered collect callback. Views
# without a saved entry start from the global debug_visualizations slot in
# Editor_settings_config, which acts as the defaults for new views.
struct("Scene_view_settings",
    version=1,
    short_desc="Scene View Settings",
    long_desc="Per scene view persisted settings, keyed by view name",
    developer=False,
    fields=[
        field("name",                 String, added_in=1, default='""', short_desc="Name"),
        field("debug_visualizations", StructRef("Debug_visualizations_settings"), added_in=1, short_desc="Debug Visualizations"),
    ],
)
