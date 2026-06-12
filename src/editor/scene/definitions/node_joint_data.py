from erhe_codegen import *

struct("Node_joint_data",
    version=1,
    fields=[
        field("node_id",           UInt64, added_in=1, default="0",     short_desc="Node this joint is attached to"),
        field("connected_node_id", UInt64, added_in=1, default="0",     short_desc="Connected node; 0 = none (constrain to world)"),
        field("joint_id",          UInt64, added_in=1, default="0",     short_desc="Physics_joint_data id; 0 = no settings (all axes free)"),
        field("enable_collision",  Bool,   added_in=1, default="false", short_desc="Allow the connected bodies to collide with each other"),
    ],
)
