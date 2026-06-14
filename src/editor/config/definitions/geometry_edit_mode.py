from erhe_codegen import *

# Controls what happens when mesh component (vertex / edge / face) editing moves
# vertices of a Geometry that is shared by more than one mesh (e.g. a duplicated
# node, or two brush instances at the same scale). 'shared' edits the shared
# Geometry so every instance changes together; 'fork' deep-copies the Geometry for
# the edited instance on the first move, so only that instance changes. Surfaced in
# the scene-view toolbar next to the component selection mode.
enum("Geometry_edit_mode",
    value("shared", 0, short_desc="Edit shared geometry (all instances change)"),
    value("fork",   1, short_desc="Fork geometry on edit (only this instance changes)"),
    underlying_type=UInt,
)
