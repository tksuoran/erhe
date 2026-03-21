from erhe_codegen import *

struct("Gltf_source_reference",
    field("gltf_path",  String, added_in=1, default='""', short_desc="Relative path to glTF file"),
    field("item_name",  String, added_in=1, default='""', short_desc="Name of the item in glTF"),
    field("item_index", Int,    added_in=1, default="-1", short_desc="Index of the item in glTF"),
    field("item_type",  String, added_in=1, default='""', short_desc="Type: mesh, material, animation, skin, texture"),
    version=1,
)
