from erhe_codegen import *

# Controls what the transform gizmo does to a mesh component (vertex / edge / face)
# selection: 'move' translates / rotates / scales the selected components in place;
# 'extrude' first duplicates the selection boundary and bridges it with new faces
# (quads for edges and face boundaries, 2-vertex polygons for vertices), then moves
# the duplicated components along the gizmo delta; 'extrude_group_normal' performs the
# same topology change but then slides each disjoint (connected) selection subset along
# that subset's own average normal, by an amount derived from the gizmo drag distance;
# 'extrude_vertex_normal' performs the same topology change but slides each vertex along
# its own original vertex normal (so vertices fan out individually instead of moving as
# a rigid subset). Surfaced in the scene-view toolbar next to the component selection mode.
enum("Mesh_transform_mode",
    value("move",                  0, short_desc="Move selected components"),
    value("extrude",               1, short_desc="Extrude selected components, then move"),
    value("extrude_group_normal",  2, short_desc="Extrude each disjoint subset along its average normal"),
    value("extrude_vertex_normal", 3, short_desc="Extrude each vertex along its own normal"),
    underlying_type=UInt,
)
