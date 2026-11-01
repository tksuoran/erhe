from erhe_codegen import *

struct("Selection_config",
    reflect=True,
    version=1,
    short_desc="Selection",
    long_desc="Selection behavior.",
    developer=False,
    fields=[
        field(
            "openxr_selection_mode",
            EnumRef("Xr_selection_mode"),
            added_in=1,
            default="Xr_selection_mode::single",
            short_desc="OpenXR Selection Mode",
            long_desc="What a headset controller select (right A) does: Single selects the pointed object alone (or deselects it when it is the selected one), keeping 0 or 1 objects selected; Toggle adds or removes the pointed object.",
            visible=True,
            developer=False
        ),
    ]
)
