from erhe_codegen import *

enum("Layout_type_serial",
    value("stack", 0),
    value("grid",  1),
    value("flow",  2),
)

enum("Axis_direction_serial",
    value("pos_x", 0),
    value("neg_x", 1),
    value("pos_y", 2),
    value("neg_y", 3),
    value("pos_z", 4),
    value("neg_z", 5),
)

enum("Layout_alignment_serial",
    value("negative", 0),
    value("positive", 1),
    value("stretch",  2),
)

struct("Layout_data",
    version=1,
    fields=[
        field("node_id",             UInt64,                              added_in=1, default="0", short_desc="Node this layout is attached to"),
        field("name",                String,                              added_in=1, default='""'),
        field("type",                EnumRef("Layout_type_serial"),       added_in=1, default="Layout_type_serial::stack"),
        field("volume_min",          Vec3,                                added_in=1, default="-0.5f, -0.5f, -0.5f"),
        field("volume_max",          Vec3,                                added_in=1, default="0.5f, 0.5f, 0.5f"),
        field("primary",             EnumRef("Axis_direction_serial"),    added_in=1, default="Axis_direction_serial::pos_x"),
        field("secondary",           EnumRef("Axis_direction_serial"),    added_in=1, default="Axis_direction_serial::pos_y"),
        field("tertiary",            EnumRef("Axis_direction_serial"),    added_in=1, default="Axis_direction_serial::pos_z"),
        field("gap",                 Vec3,                                added_in=1, default="0.0f, 0.0f, 0.0f"),
        field("grid_track_count",    IVec3,                               added_in=1, default="1, 1, 1"),
        field("grid_track_extent_x", Vector(Float),                       added_in=1, short_desc="Per-track extents on X; empty = uniform"),
        field("grid_track_extent_y", Vector(Float),                       added_in=1, short_desc="Per-track extents on Y; empty = uniform"),
        field("grid_track_extent_z", Vector(Float),                       added_in=1, short_desc="Per-track extents on Z; empty = uniform"),
    ],
)

struct("Layout_item_data",
    version=1,
    fields=[
        field("node_id",        UInt64,                              added_in=1, default="0", short_desc="Node this layout item is attached to"),
        field("name",           String,                              added_in=1, default='""'),
        field("align_x",        EnumRef("Layout_alignment_serial"),  added_in=1, default="Layout_alignment_serial::negative"),
        field("align_y",        EnumRef("Layout_alignment_serial"),  added_in=1, default="Layout_alignment_serial::negative"),
        field("align_z",        EnumRef("Layout_alignment_serial"),  added_in=1, default="Layout_alignment_serial::negative"),
        field("margin_min",     Vec3,                                added_in=1, default="0.0f, 0.0f, 0.0f"),
        field("margin_max",     Vec3,                                added_in=1, default="0.0f, 0.0f, 0.0f"),
        field("grid_cell_auto", Bool,                                added_in=1, default="true"),
        field("grid_cell",      IVec3,                               added_in=1, default="0, 0, 0"),
        field("grid_span",      IVec3,                               added_in=1, default="1, 1, 1"),
    ],
)
