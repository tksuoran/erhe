from erhe_codegen import *

struct("Grid_config",
    version=3,
    short_desc="Grid",
    long_desc="",
    developer=False,
    fields=[
        field(
            "snap_enabled",
            Bool,
            added_in=1,
            default="true",
            short_desc="Snap Enable",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "visible",
            Bool,
            added_in=1,
            default="true",
            short_desc="Visible",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "major_color",
            Vec4,
            added_in=1,
            removed_in=3,
            default="1.0f, 1.0f, 1.0f, 1.0f",
            short_desc="Major Grid Line Color",
            long_desc="Deprecated, replaced by per-level colors",
            visible=False,
            developer=False
        ),
        field(
            "minor_color",
            Vec4,
            added_in=1,
            removed_in=3,
            default="0.5f, 0.5f, 0.5f, 0.5f",
            short_desc="Minor Grid Line Color",
            long_desc="Deprecated, replaced by per-level colors",
            visible=False,
            developer=False
        ),
        field(
            "major_width",
            Float,
            added_in=1,
            removed_in=3,
            default="4.0f",
            short_desc="Major Grid Line Width",
            long_desc="Deprecated, replaced by per-level widths",
            visible=False,
            developer=False
        ),
        field(
            "minor_width",
            Float,
            added_in=1,
            removed_in=3,
            default="2.0f",
            short_desc="Minor Grid Line width",
            long_desc="Deprecated, replaced by per-level widths",
            visible=False,
            developer=False
        ),
        field(
            "cell_size",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Grid Cell Size",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "cell_div",
            Int,
            added_in=1,
            default="10",
            short_desc="Grid Cell Subdivision",
            long_desc="How many Minor Cells are in one Major Cell",
            visible=True,
            developer=False
        ),
        field(
            "cell_count",
            Int,
            added_in=1,
            default="2",
            short_desc="Grid Cell Count per Coordinate Axis",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "label_enable",
            Bool,
            added_in=2,
            default="true",
            short_desc="Axis Label Enable",
            long_desc="Show axis coordinate labels in the grid shader",
            visible=True,
            developer=False
        ),
        field(
            "label_text_fraction",
            Float,
            added_in=2,
            default="0.15f",
            short_desc="Axis Label Size",
            long_desc="Label text height as a fraction of label spacing",
            visible=True,
            developer=False
        ),
        field(
            "label_spacing",
            Float,
            added_in=2,
            default="1.0f",
            short_desc="Axis Label Spacing",
            long_desc="Label spacing in world units (integer >= 1)",
            visible=True,
            developer=False
        ),
        field(
            "label_fade",
            Float,
            added_in=2,
            default="4.0f",
            short_desc="Axis Label Fade",
            long_desc="Glyph size in pixels per em at which labels are fully visible; smaller values keep labels visible further away",
            visible=True,
            developer=False
        ),
        field(
            "label_color",
            Vec4,
            added_in=2,
            default="0.0f, 0.0f, 0.0f, 1.0f",
            short_desc="Axis Label Color",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "level0_color",
            Vec4,
            added_in=2,
            default="0.0f, 0.0f, 0.01f, 1.0f",
            short_desc="Level 0 Line Color",
            long_desc="Line color for the coarsest grid LOD level",
            visible=True,
            developer=False
        ),
        field(
            "level1_color",
            Vec4,
            added_in=2,
            default="0.0f, 0.0f, 0.0f, 1.0f",
            short_desc="Level 1 Line Color",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "level2_color",
            Vec4,
            added_in=2,
            default="0.01f, 0.0f, 0.0f, 1.0f",
            short_desc="Level 2 Line Color",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "level3_color",
            Vec4,
            added_in=2,
            default="0.0f, 0.01f, 0.0f, 1.0f",
            short_desc="Level 3 Line Color",
            long_desc="Line color for the finest grid LOD level",
            visible=True,
            developer=False
        ),
        field(
            "level0_width",
            Float,
            added_in=3,
            default="0.006f",
            short_desc="Level 0 Line Width",
            long_desc="Line width for the coarsest grid LOD level, as a fraction of the level cell size",
            visible=True,
            developer=False,
            ui_min="0.0f",
            ui_max="0.5f"
        ),
        field(
            "level1_width",
            Float,
            added_in=3,
            default="0.02f",
            short_desc="Level 1 Line Width",
            long_desc="Line width as a fraction of the level cell size",
            visible=True,
            developer=False,
            ui_min="0.0f",
            ui_max="0.5f"
        ),
        field(
            "level2_width",
            Float,
            added_in=3,
            default="0.02f",
            short_desc="Level 2 Line Width",
            long_desc="Line width as a fraction of the level cell size",
            visible=True,
            developer=False,
            ui_min="0.0f",
            ui_max="0.5f"
        ),
        field(
            "level3_width",
            Float,
            added_in=3,
            default="0.02f",
            short_desc="Level 3 Line Width",
            long_desc="Line width for the finest grid LOD level, as a fraction of the level cell size",
            visible=True,
            developer=False,
            ui_min="0.0f",
            ui_max="0.5f"
        ),
    ],
)
