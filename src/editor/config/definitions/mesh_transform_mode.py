from erhe_codegen import *

# Controls what the transform gizmo does to a mesh component (vertex / edge / face)
# selection: 'move' translates / rotates / scales the selected components in place;
# 'extrude' first duplicates the selection boundary and bridges it with new faces
# (quads for edges and face boundaries, 2-vertex polygons for vertices), then moves
# the duplicated components. Surfaced in the scene-view toolbar next to the mesh
# component selection mode.
enum("Mesh_transform_mode",
    value("move",    0, short_desc="Move selected components"),
    value("extrude", 1, short_desc="Extrude selected components, then move"),
    underlying_type=UInt,
)
