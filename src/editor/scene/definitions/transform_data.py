from erhe_codegen import *

struct("Transform_data",
    field("translation", Vec3, added_in=1, default="0.0f, 0.0f, 0.0f"),
    field("rotation",    Vec4, added_in=1, default="0.0f, 0.0f, 0.0f, 1.0f", short_desc="Quaternion xyzw"),
    field("scale",       Vec3, added_in=1, default="1.0f, 1.0f, 1.0f"),
    version=1,
)
