from erhe_codegen import *

# Placement settings of the Level Kit window and the initial values of a new kit layout node
# (doc/editor/level_tools.md "Level Kit window").
struct("Level_kit_config",
    version=8,
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
        field("layout_pattern",           String, added_in=5, default='""',    short_desc="Layout Pattern",       long_desc="The pattern of a new kit layout node as its canonical text; empty for the defaults."),
        field("layout_spacing_from_item", Bool,   added_in=5, default="true",  short_desc="Spacing From Item",    long_desc="A new kit layout node's line step and grid spacings are its item's extent, a contiguous run."),
        field("layout_module_snap",       Bool,   added_in=5, default="false", short_desc="Layout Module Snap",   long_desc="A new kit layout node rounds its element positions to the module."),
        field("layout_surface_snap",      Bool,   added_in=5, default="false", short_desc="Layout Surface Snap",  long_desc="A new kit layout node snaps each element to the surface under it."),
        field("layout_surface_max_distance", Float, added_in=6, default="1.0f", short_desc="Layout Surface Max Distance", long_desc="Meters: a new kit layout node's elements move at most this far to snap to a surface."),
        field("layout_surface_orient",    Bool,   added_in=5, default="false", short_desc="Layout Surface Orient", long_desc="A new kit layout node turns each element's up axis to the surface normal."),
        field("layout_per_piece_seed",    Bool,   added_in=5, default="false", renamed_from=[(7, "layout_random_seeds")], short_desc="Layout Per Piece Seed", long_desc="A new kit layout node gives each element a seed of its own from the layout seed."),
        field("layout_seed",              Int,    added_in=5, default="0",     short_desc="Layout Seed",          long_desc="The layout seed of a new kit layout node."),
        field("layout_locked",            Bool,   added_in=5, default="true",  short_desc="Layout Locked",        long_desc="A new kit layout node is locked: it makes its children."),
        field("layout_hide_all_connectors",            Bool, added_in=8, default="false", short_desc="Layout Hide All Connectors",       long_desc="A new path or segments layout hides its children's connectors, an open curve's first and last child keeping all but the one facing its neighbour."),
        field("layout_hide_plus_tangent_connectors",   Bool, added_in=8, default="false", short_desc="Layout Hide +Tangent Connectors",  long_desc="A new path or segments layout hides its children's connectors facing along the curve."),
        field("layout_hide_minus_tangent_connectors",  Bool, added_in=8, default="false", short_desc="Layout Hide -Tangent Connectors",  long_desc="A new path or segments layout hides its children's connectors facing back along the curve."),
        field("layout_hide_plus_normal_connectors",    Bool, added_in=8, default="false", short_desc="Layout Hide +Normal Connectors",   long_desc="A new path or segments layout hides its inner children's connectors facing the right of the curve's direction."),
        field("layout_hide_minus_normal_connectors",   Bool, added_in=8, default="false", short_desc="Layout Hide -Normal Connectors",   long_desc="A new path or segments layout hides its inner children's connectors facing the left of the curve's direction."),
        field("layout_hide_plus_binormal_connectors",  Bool, added_in=8, default="false", short_desc="Layout Hide +Binormal Connectors", long_desc="A new path or segments layout hides its inner children's connectors facing up."),
        field("layout_hide_minus_binormal_connectors", Bool, added_in=8, default="false", short_desc="Layout Hide -Binormal Connectors", long_desc="A new path or segments layout hides its inner children's connectors facing down."),
        field("place_as_layout",          Bool,   added_in=5, default="false", short_desc="Place As Layout",      long_desc="A click of the Brush tool in kit mode places a kit layout node of the chosen shape with these values instead of one piece."),
    ],
)
