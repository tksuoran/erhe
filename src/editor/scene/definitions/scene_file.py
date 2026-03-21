from erhe_codegen import *

struct("Scene_file",
    field("name",            String,                                    added_in=1, default='""'),
    field("enable_physics",  Bool,                                      added_in=1, default="true"),
    field("nodes",           Vector(StructRef("Node_data_serial")),     added_in=1),
    field("cameras",         Vector(StructRef("Camera_data")),          added_in=1),
    field("lights",          Vector(StructRef("Light_data")),           added_in=1),
    field("mesh_references", Vector(StructRef("Mesh_reference")),       added_in=1),
    field("node_physics",    Vector(StructRef("Node_physics_data")),    added_in=2),
    version=2,
)
