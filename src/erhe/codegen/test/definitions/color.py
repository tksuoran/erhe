from erhe_codegen import *

struct("Color",
    version=1,
    fields=[
        field("r", Float, added_in=1, default="0.0f", short_desc="Red"),
        field("g", Float, added_in=1, default="0.0f", short_desc="Green"),
        field("b", Float, added_in=1, default="0.0f", short_desc="Blue"),
        field("a", Float, added_in=1, default="1.0f", short_desc="Alpha"),
    ],
)
