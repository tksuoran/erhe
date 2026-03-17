from erhe_codegen import *

enum("Blend_mode",
    value("opaque",       0, short_desc="Opaque",      long_desc="No transparency"),
    value("alpha_blend",  1, short_desc="Alpha Blend",  long_desc="Standard alpha blending"),
    value("additive",     2, short_desc="Additive",     long_desc="Additive blending for glow effects"),
    value("multiply",     3, short_desc="Multiply",     long_desc="Multiplicative blending"),
    underlying_type=UInt8,
)
