from erhe_codegen import *

struct("Grid_config",
    version=1,
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
            default="1.0f, 1.0f, 1.0f, 1.0f",
            short_desc="Major Grid Line Color",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "minor_color",
            Vec4,
            added_in=1,
            default="0.5f, 0.5f, 0.5f, 0.5f",
            short_desc="Minor Grid Line Color",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "major_width",
            Float,
            added_in=1,
            default="4.0f",
            short_desc="Major Grid Line Width",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "minor_width",
            Float,
            added_in=1,
            default="2.0f",
            short_desc="Minor Grid Line width",
            long_desc="",
            visible=True,
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
    ],
)
