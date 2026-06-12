from erhe_codegen import *

struct("Scene_file",
    version=3,
    fields=[
        field("name",              String,                                     added_in=1, default='""'),
        field("enable_physics",    Bool,                                       added_in=1, default="true"),
        field("nodes",             Vector(StructRef("Node_data_serial")),      added_in=1),
        field("cameras",           Vector(StructRef("Camera_data")),           added_in=1),
        field("lights",            Vector(StructRef("Light_data")),            added_in=1),
        field("mesh_references",   Vector(StructRef("Mesh_reference")),        added_in=1),
        field("node_physics",      Vector(StructRef("Node_physics_data")),     added_in=1),
        field("layouts",           Vector(StructRef("Layout_data")),           added_in=2),
        field("layout_items",      Vector(StructRef("Layout_item_data")),      added_in=2),
        field("physics_materials", Vector(StructRef("Physics_material_data")), added_in=3, short_desc="Shared physics materials referenced by node_physics"),
        field("collision_filters", Vector(StructRef("Collision_filter_data")), added_in=3, short_desc="Shared collision filters referenced by node_physics"),
        field("physics_joints",    Vector(StructRef("Physics_joint_data")),    added_in=3, short_desc="Shared joint settings referenced by node_joints"),
        field("node_joints",       Vector(StructRef("Node_joint_data")),       added_in=3),
    ],
)
