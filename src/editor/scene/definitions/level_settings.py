from erhe_codegen import *

# The level's two tunable lengths (doc/editor/level_tools.md "Level settings").
# Kit shapes state their default sizes in modules and wall heights; a newly
# placed piece resolves them against these values.
struct("Level_settings",
    version=1,
    short_desc="Level settings",
    long_desc="The lengths kit shape defaults resolve against.",
    developer=False,
    fields=[
        field("module",      Float, added_in=1, default="1.0f", short_desc="Module",      long_desc="The level's unit of length in meters; snap positions and modular default sizes follow it."),
        field("wall_height", Float, added_in=1, default="3.0f", short_desc="Wall Height", long_desc="The default wall height in meters."),
    ],
)
