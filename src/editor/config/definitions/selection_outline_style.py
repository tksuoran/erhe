from erhe_codegen import *

# Editor-global Selection Outline appearance: the pulsing highlight drawn
# around selected meshes. Split out of the per-Scene_view Viewport_config and
# shared by all scene views. Stored in Editor_settings_config (selection_outline)
# and read by renderers/composition_pass.cpp, which animates between the Low and
# High color / width at Frequency. Negative widths are constant screen-space
# pixel widths.
struct("Selection_outline_style",
    version=1,
    short_desc="Selection Outline",
    long_desc="Editor-global appearance of the selection outline highlight",
    developer=False,
    fields=[
        field("selection_highlight_low",       Vec4,  added_in=1, default="2.0f, 1.0f, 0.0f, 0.8f", short_desc="Color Low",  long_desc="Outline color at the low end of the pulse."),
        field("selection_highlight_high",      Vec4,  added_in=1, default="2.0f, 1.0f, 0.0f, 0.8f", short_desc="Color High", long_desc="Outline color at the high end of the pulse."),
        field("selection_highlight_width_low",  Float, added_in=1, default="-3.0f", short_desc="Width Low",  long_desc="Outline width at the low end of the pulse. Negative is a constant screen-space pixel width."),
        field("selection_highlight_width_high", Float, added_in=1, default="-3.0f", short_desc="Width High", long_desc="Outline width at the high end of the pulse. Negative is a constant screen-space pixel width."),
        field("selection_highlight_frequency",  Float, added_in=1, default="1.0f",  short_desc="Frequency",  long_desc="Pulse frequency (cycles per second) between the Low and High color / width."),
    ],
)
