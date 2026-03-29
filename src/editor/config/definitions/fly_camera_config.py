from erhe_codegen import *

struct("Fly_camera_config",
    version=1,
    short_desc="Fly camera transform persistence",
    long_desc="",
    developer=False,
    fields=[
        field("translation", Vec3, added_in=1, default="0.0f, 0.0f, 0.0f"),
        field("rotation",    Vec4, added_in=1, default="0.0f, 0.0f, 0.0f, 1.0f"),
    ],
)
