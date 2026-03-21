from erhe_codegen import *

struct("Renderer_config",
    field("max_material_count",  Int, added_in=1, default="1000"),
    field("max_light_count",     Int, added_in=1, default="32"),
    field("max_camera_count",    Int, added_in=1, default="256"),
    field("max_joint_count",     Int, added_in=1, default="1000"),
    field("max_primitive_count", Int, added_in=1, default="6000"),
    field("max_draw_count",      Int, added_in=1, default="6000"),
    version=1,
)
