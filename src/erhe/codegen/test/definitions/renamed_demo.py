from erhe_codegen import *

# Fields stored under earlier key names: label was "title" before v2 and
# "caption" in v2; per_piece was "random" in v2.
struct("Demo_renamed",
    version=3,
    short_desc="Demo renamed",
    long_desc="Leaf struct for renamed-key tests",
    fields=[
        field("label",     String, added_in=1, default='""',    renamed_from=[(2, "title"), (3, "caption")], short_desc="Label"),
        field("per_piece", Bool,   added_in=2, default="false", renamed_from=[(3, "random")],                short_desc="Per Piece"),
    ],
)
