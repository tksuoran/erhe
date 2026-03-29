from erhe_codegen import *

struct("Graphics_presets_config",
    version=1,
    short_desc="Collection of graphics presets",
    long_desc="",
    developer=False,
    fields=[
        field("presets", Vector(StructRef("Graphics_preset_entry")), added_in=1),
    ],
)
