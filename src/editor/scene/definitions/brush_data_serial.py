from erhe_codegen import *

struct("Brush_data_serial",
    version=1,
    fields=[
        field("name",          String, added_in=1, default='""', short_desc="Name of the brush asset"),
        field("geometry_path", String, added_in=1, default='""', short_desc="Base name of the companion .geogram file holding the brush geometry"),
        field("material_name", String, added_in=1, default='""', short_desc="Name of the referenced material in the content library (empty = none)"),
        field("density",       Float,  added_in=1, default="1.0f", short_desc="Brush density used for mass computation"),
        field("normal_style",  Int,    added_in=1, default="0",    short_desc="erhe::primitive::Normal_style used to build the brush primitive"),
    ],
)
