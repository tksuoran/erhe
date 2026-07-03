from erhe_codegen import *

struct("Graph_mesh_binding_data",
    version=1,
    fields=[
        field("node_id",         UInt64, added_in=1, default="0",  short_desc="Scene node carrying the Geometry Graph Mesh attachment (file-local id)"),
        field("graph_mesh_name", String, added_in=1, default='""', short_desc="Name of the Graph Mesh asset the node sources its mesh from"),
    ],
)
