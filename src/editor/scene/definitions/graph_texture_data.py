from erhe_codegen import *

struct("Graph_texture_data",
    version=1,
    fields=[
        field("name",  String, added_in=1, default='""', short_desc="Name of the Graph Texture asset"),
        field("graph", String, added_in=1, default='""', short_desc="The node graph serialized as a JSON string (texture-graph v1 format)"),
    ],
)
