from erhe_codegen import *

struct("Asset_reference_data",
    version=1,
    short_desc="Asset reference",
    long_desc="Serialized form of an editor Asset_key (asset-manager plan D2): scope + type plus the identity of the asset inside its container (uid first, unique name as the glTF 2.1-sanctioned fallback). An empty scope means no reference. There is deliberately no array-index field (plan decision 11).",
    developer=False,
    omit_defaults=True,
    fields=[
        field(
            "scope",
            String,
            added_in=1,
            default='""',
            short_desc="Asset scope",
            long_desc="One of: scene_local, builtin, file; empty string means no reference",
            visible=True,
            developer=False
        ),
        field(
            "asset_type",
            String,
            added_in=1,
            default='""',
            short_desc="Asset type",
            long_desc="Managed asset type name: brush, material, animation or mesh",
            visible=True,
            developer=False
        ),
        field(
            "path",
            String,
            added_in=1,
            default='""',
            short_desc="Container path",
            long_desc="Container file path for file-scope references; portable forward-slash string; empty for builtin / scene_local",
            visible=True,
            developer=False
        ),
        field(
            "uid",
            String,
            added_in=1,
            default='""',
            short_desc="Unique ID",
            long_desc="glTF 2.1 unique ID of the asset within its container; primary identity when present",
            visible=True,
            developer=False
        ),
        field(
            "name",
            String,
            added_in=1,
            default='""',
            short_desc="Asset name",
            long_desc="Asset name; fallback identity (must be unique within the container and type to serve as an identifier)",
            visible=True,
            developer=False
        ),
    ],
)
