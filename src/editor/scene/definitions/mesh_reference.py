from erhe_codegen import *

struct("Mesh_reference",
    field("node_id",         UInt64,                                     added_in=1, default="0",  short_desc="Which node this mesh is attached to"),
    field("gltf_source",     StructRef("Gltf_source_reference"),         added_in=1),
    field("material_refs",   Vector(StructRef("Gltf_source_reference")), added_in=1, short_desc="Per-primitive material references"),
    field("source_type",     EnumRef("Mesh_source_type"),                added_in=2, default="Mesh_source_type::gltf",  short_desc="Normative data source"),
    field("geometry_path",   String,                                     added_in=2, default='""', short_desc="Base path for geogram mesh files (relative)"),
    field("mesh_name",       String,                                     added_in=2, default='""', short_desc="Mesh name"),
    field("primitive_count", Int,                                        added_in=2, default="0",  short_desc="Number of primitives in the mesh"),
    version=2,
)
