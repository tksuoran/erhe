from erhe_codegen import *

enum("Drive_type_serial",
    value("e_linear",  0, short_desc="Drive acts on a translation axis"),
    value("e_angular", 1, short_desc="Drive acts on a rotation axis"),
)
