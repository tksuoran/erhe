from erhe_codegen import *

struct("Item_placement_config",
    reflect=True,
    version=1,
    short_desc="Create / Paste Placement",
    long_desc="Where Create and Paste of a viewport's context menu put an item (doc/editor/windows.md \"Viewport context menu\").",
    developer=False,
    fields=[
        field(
            "mode",
            EnumRef("Item_placement_mode"),
            added_in=1,
            default="Item_placement_mode::avoid_intersection",
            short_desc="Placement Mode",
            long_desc="Origin puts the item's origin on the hovered point. Avoid Intersection then moves the item along the hovered surface's normal until its bounds are clear of the surface.",
            visible=True,
            developer=False
        ),
    ]
)
