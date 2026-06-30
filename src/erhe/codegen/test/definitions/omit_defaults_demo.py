from erhe_codegen import *

# Leaf struct exercising omit-defaults serialization and a versioned default.
# bias changed its default from 0.25f (v1) to 0.5f (v2): documents written at
# _version 1 with bias absent must reconstruct 0.25f, not the current 0.5f.
struct("Demo_params",
    version=2,
    short_desc="Demo params",
    long_desc="Leaf struct for omit-defaults and versioned-default tests",
    omit_defaults=True,
    fields=[
        field("scale",   Float, added_in=1, default="1.0f", short_desc="Scale"),
        field("count",   Int,   added_in=1, default="3",    short_desc="Count"),
        field("bias",    Float, added_in=1, default="0.5f", default_history=[(2, "0.25f")], short_desc="Bias"),
        field("enabled", Bool,  added_in=1, default="true", short_desc="Enabled"),
    ],
)

# Container struct mirroring the inventory-slot shape: a string plus an embedded
# omit-defaults leaf struct. An all-default slot serializes to an empty object.
struct("Demo_slot",
    version=1,
    short_desc="Demo slot",
    long_desc="Container struct embedding Demo_params for nested-omission tests",
    omit_defaults=True,
    fields=[
        field("name",   String,               added_in=1, default='""', short_desc="Name"),
        field("params", StructRef("Demo_params"), added_in=1,           short_desc="Params"),
    ],
)
