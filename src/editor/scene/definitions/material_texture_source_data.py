from erhe_codegen import *

struct("Material_texture_source_data",
    version=1,
    fields=[
        field("material_name",      String, added_in=1, default='""', short_desc="Name of the material whose slot is bound"),
        field("slot",               String, added_in=1, default='""', short_desc="Texture slot: base_color, metallic_roughness, normal, occlusion, or emissive"),
        field("graph_texture_name", String, added_in=1, default='""', short_desc="Name of the Graph Texture asset the slot samples"),
    ],
)
