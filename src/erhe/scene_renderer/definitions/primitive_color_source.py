from erhe_codegen import *

enum("Primitive_color_source",
    value("id_offset",            0, short_desc="ID Offset"),
    value("mesh_wireframe_color", 1, short_desc="Mesh Wireframe color"),
    value("constant_color",       2, short_desc="Constant Color"),
    underlying_type=UInt,
)
