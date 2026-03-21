from erhe_codegen import *

struct("Node_data_serial",
    field("name",      String,                       added_in=1, default='""'),
    field("id",        UInt64,                       added_in=1, default="0",    short_desc="Unique node ID within scene file"),
    field("parent_id", UInt64,                       added_in=1, default="0",    short_desc="Parent node ID, 0 = root"),
    field("transform", StructRef("Transform_data"),  added_in=1),
    field("flag_bits", UInt64,                       added_in=1, default="0"),
    version=1,
)
