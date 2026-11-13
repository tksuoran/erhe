from erhe_codegen import *

# Placement settings of the Level Kit window (doc/editor/level_tools.md "Level Kit window").
struct("Level_kit_config",
    version=4,
    short_desc="Level Kit",
    long_desc="Placement settings of the Brush tool's kit mode (Level Kit window).",
    developer=False,
    fields=[
        field("snap_to_module",    Bool, added_in=1, default="true",  short_desc="Snap To Module",    long_desc="Round the placement position to multiples of the scene's module."),
        # snap_to_surface and orient_to_surface (v1 - v3) moved to Surface_snap_config
        # (editor settings v20); an older file's keys are ignored on load. (Deleted
        # rather than marked removed_in, as in load_config.py.)
        field("rotation_step_degrees", Int, added_in=2, default="90", short_desc="Rotation Step", long_desc="Degrees the Brush tool's rotate commands turn a kit piece about the up axis: 90, 45 or 15."),
        field("new_seed_per_piece", Bool, added_in=3, default="false", short_desc="New Seed Per Piece", long_desc="After each placed piece the Brush tool's kit values take the next seed, so a shape with values derived from the seed gives varied pieces."),
    ],
)
