from erhe_codegen import *

enum("Combine_mode_serial",
    value("e_average",  0, short_desc="Average of the two material values"),
    value("e_minimum",  1),
    value("e_maximum",  2),
    value("e_multiply", 3),
)
