from erhe_codegen import *

# Procedural default window layout, applied when no persisted ImGui ini layout
# exists. Loaded from config/editor/default_layout.json by
# editor_default_layout.cpp.
struct("Default_layout_config",
    version=1,
    short_desc="Procedural default window layout",
    long_desc="",
    developer=False,
    fields=[
        field("placements", Vector(StructRef("Dock_placement")), added_in=1),
    ],
)
