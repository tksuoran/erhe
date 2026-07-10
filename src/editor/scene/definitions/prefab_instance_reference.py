from erhe_codegen import *

struct("Prefab_instance_reference",
    version=1,
    fields=[
        field("node_id",     UInt64, added_in=1, default="0",  short_desc="Scene node that is the prefab instance root (file-local id)"),
        field("source_path", String, added_in=1, default='""', short_desc="Prefab source glTF path, relative to the bundle directory when possible"),
        field("prefab_name", String, added_in=1, default='""', short_desc="Prefab display name"),
    ],
)
