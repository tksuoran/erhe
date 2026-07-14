from erhe_codegen import *

# One entry of the procedural default window layout. Entries are applied in
# order; each entry places one window relative to a window placed by an
# earlier entry (or relative to the root dockspace when target is empty).
# See editor_default_layout.cpp.
struct("Dock_placement",
    version=1,
    short_desc="Procedural default layout entry",
    long_desc="",
    developer=False,
    fields=[
        field(
            "window",
            String,
            added_in=1,
            default='""',
            short_desc="ImGui title of the window to place",
            long_desc="The special value \"$primary_viewport\" resolves to the primary viewport window title at runtime",
        ),
        field(
            "target",
            String,
            added_in=1,
            default='""',
            short_desc="ImGui title of a previously placed window to dock relative to; empty targets the root dockspace",
        ),
        field(
            "direction",
            EnumRef("Dock_direction"),
            added_in=1,
            default="Dock_direction::none",
            short_desc="Side of the target to split off; none tabs into the target's dock node",
        ),
        field(
            "fraction",
            Float,
            added_in=1,
            default="0.0f",
            short_desc="Fraction of the ROOT dockspace size (width for left/right, height for up/down) the window takes",
        ),
    ],
)
