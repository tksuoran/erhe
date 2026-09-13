from erhe_codegen import *

# One variant selection of a scene (Scene_settings::variant_selections,
# doc/usd-compatibility-plan.md X4). `prim_path` is the M1 path of the prim
# carrying the variant set, `set_name` the set's name and `variant_name` the
# variant chosen for it. A set without an entry keeps the selection the file
# it came from authored; an entry is written by Scene_root::select_variant
# and is what a saved scene carries the selection in.
#
# A variant block is free to declare a variant set of its own, which is a set
# of the same prim (doc/usd-compatibility-plan.md section 6, "Variant opinions
# a variant set does not carry"), so the set name alone does not name a set:
# two blocks of one set may each declare a nested set of the same name.
# `enclosing_set_name` / `enclosing_variant_name` are the block the set is
# declared inside, both empty for a set the prim declares itself - which is
# what a file written before v2 holds, so it reads unchanged.
struct("Variant_selection",
    version=2,
    short_desc="Variant set selection",
    long_desc="",
    developer=False,
    fields=[
        field("prim_path",              String, added_in=1, default='""', short_desc="Path of the prim carrying the variant set"),
        field("set_name",               String, added_in=1, default='""', short_desc="Variant set name"),
        field("variant_name",           String, added_in=1, default='""', short_desc="Selected variant name"),
        field("enclosing_set_name",     String, added_in=2, default='""', short_desc="Name of the variant set whose block declares this set", long_desc="Empty when the prim declares the set itself."),
        field("enclosing_variant_name", String, added_in=2, default='""', short_desc="Name of the variant block this set is declared inside", long_desc="Empty when the prim declares the set itself."),
    ],
)
