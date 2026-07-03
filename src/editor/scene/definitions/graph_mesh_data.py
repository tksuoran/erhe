from erhe_codegen import *

struct("Graph_mesh_data",
    version=1,
    fields=[
        field("name",  String, added_in=1, default='""', short_desc="Name of the Graph Mesh asset"),
        field("graph", String, added_in=1, default='""', short_desc="The node graph serialized as a JSON string (geometry-graph v1 format)"),
    ],
)
