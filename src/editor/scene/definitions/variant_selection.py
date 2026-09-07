from erhe_codegen import *

# One variant selection of a scene (Scene_settings::variant_selections,
# doc/usd-compatibility-plan.md X4). `prim_path` is the M1 path of the prim
# carrying the variant set, `set_name` the set's name and `variant_name` the
# variant chosen for it. A set without an entry keeps the selection the file
# it came from authored; an entry is written by Scene_root::select_variant
# and is what a saved scene carries the selection in.
struct("Variant_selection",
    version=1,
    short_desc="Variant set selection",
    long_desc="",
    developer=False,
    fields=[
        field("prim_path",    String, added_in=1, default='""', short_desc="Path of the prim carrying the variant set"),
        field("set_name",     String, added_in=1, default='""', short_desc="Variant set name"),
        field("variant_name", String, added_in=1, default='""', short_desc="Selected variant name"),
    ],
)
