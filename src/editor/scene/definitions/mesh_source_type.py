from erhe_codegen import *

enum("Mesh_source_type",
    value("geometry", 0, short_desc="Normative source is Geometry (geogram)"),
    value("gltf",     1, short_desc="Normative source is glTF (triangle soup)"),
)
