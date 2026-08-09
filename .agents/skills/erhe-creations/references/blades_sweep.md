# Blades & leaves: the sweep shape

Swept closed profiles for organic blade shapes (leaves, vines, trim).
Reference code: creation 17's agave rosettes (2026-08-09).

- **Blades / leaves = `create_shape sweep`**: a closed CCW cross-section
  polyline (`profile`, sharp corners stay sharp - author smooth arcs as
  dense points) swept along a bezier `spine` with parallel-transported
  frames; `taper` (t, scale) keys with a final ~0 collapse the tip into
  the terminal spine point; `twist_deg`, optional `profile_end` morph.
  All sweep params are in common.py's SHAPE_GEOMETRY_KEYS, so pooled
  `c.shape("sweep", ...)` calls share one brush per unique blade.
- Agave leaf: channeled crescent profile (upper face dips, convex belly,
  margin corners), 4-point outward-curving spine, taper
  [[0, .85], [.25, 1], [.7, .6], [1, 0]].
- A rosette shares ONE pooled blade brush - pitch, yaw and bake scale
  are per-instance. Leaf local +X is the curve direction; yaw about Y
  maps +X to (cos, 0, -sin), so leaf yaw = ring angle - 90 deg.
- Rosette rings: outer leaves long and near-horizontal, inner short and
  steep; per-ring yaw offset staggers the spiral (creation 17's rings:
  9 leaves at 68 deg pitch, 7 at 50, 5 at 32, 3 at 14, lengths
  1.0/0.9/0.75/0.6).
- Sweep brushes build with SMOOTH normals: blades / vines / trim read as
  one fair surface; sharp profile corners still shade as creases via
  edge angles.
