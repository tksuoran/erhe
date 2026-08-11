# Geometry-graph sculpting: smooth organic bodies

Building a smooth organic solid (a fish body) as a live geometry graph via
MCP. Creation 18 (fish) is the reference implementation.

- **Pipeline**: `c.geometry_graph(name)` then `box -> lattice -> subdivide ->
  output` (`g.add` + `g.chain`). Box cage sized to the body's bounding
  proportions; lattice sculpts the cage; `subdivide`
  (mode 0 = catmull_clark) delivers the fair surface. Bind
  with `c.bind_node_mesh(node_name, graph_name)`, then `move_node` - the
  bound node appears at the origin.
- **ROUNDNESS RULE (user-established, 2026-08-11, dolphin)**: for a
  round organic look keep box subdivisions LOW - often 0 on at least
  one axis (e.g. trunk [2,0,0], fins [1,0,2]) - and use a SINGLE
  Catmull-Clark iteration per part. Dense box subdivisions pin the
  subdivision limit surface to the box shape, reading increasingly
  SQUARE no matter how many CC levels follow. Only add box
  subdivisions where the lattice needs interior stations along its
  squeeze axis.
- **`smooth_normals` node** (043366e0 + 881d14e4): explicit
  smooth-normal pass (topology/positions/UVs untouched). Put one
  between the last subdivision and the output so the baked surface
  shades smooth regardless of upstream nodes' post-processing.
  SHADING-NORMAL TRUTH (cost a faceted-dolphin round): the
  renderable-mesh build resolves the lighting normal per corner as
  corner_normal > facet_normal > vertex_normal (plain) > computed
  facet normal; `vertex_normal_smooth` feeds ONLY the separate
  smooth-normal vertex slot (edge lines). An op that wants smooth
  SHADING must write vertex_normal and clear corner/facet normals -
  computing vertex_normal_smooth alone changes nothing visible.
- **Lattice node parameters go through `geometry_graph_set_parameter` as one
  JSON object**: `auto_fit`, `divisions` [dx,dy,dz], `interpolation`
  (0 trilinear, 1 bezier), `show_cage` (set false - the cage renders as
  debug lines in screenshots), and `offsets` - a FLAT float array of length
  (dx+1)(dy+1)(dz+1)*3 in `lattice_offset_index` order:
  `index = i + (dx+1) * (j + (dy+1) * k)`. Divisions + offsets can be set
  in the same call (divisions applies first, then offsets length-checked).
- **Station-squeeze sculpting**: for a spindle body along X, author per-
  station (i) squeeze fractions toward the centerline and write
  `dy = -y * squeeze_y[i]`, `dz = -z * squeeze_z[i]` per control point.
  Bezier FFD (Bernstein over the whole grid) smooths interior stations
  heavily - endpoint stations apply exactly, so pinch the caps HARD
  (0.88-0.94) or they stay blunt; interior values act as soft weights.
- **Round the caps with axial shifts**: a squeezed box cap still subdivides
  into a flat swirl-pole face. Add `dx` pulling the cap stations toward the
  body center (e.g. -0.28 at the nose of a 3 m body) so subdivision rounds
  them.
- **Cross-section shaping**: uniform z-squeeze leaves a flat-topped box
  back that shades as a dark band. Add a per-height-row extra beam squeeze
  (`z_squeeze = sz[i] + (1-sz[i]) * edge_taper[j]`, e.g. taper [0.45 belly,
  0.0 mid, 0.78 back]) to get the oval fish section.
- Body-relative attachments: read `c.subtree_world_aabb(body)` for tail/top
  anchors and probe the real surface with `c.closest_points` for flank/back
  anchors (the subdivided surface sits well inside the cage - guessing
  offsets from cage numbers misses).
- **Limb chains that must land exactly** (frog, 2026-08-11): when a
  limb's far end has a required world position (a foreleg tip on the
  ground, a foot at a wrist), author the anchor SEGMENT (shoulder ->
  wrist points), align the limb's long axis to it with an align-+X
  quaternion (axis = cross(+X, dir), angle = acos(dx)) and place the
  next part at the same wrist point - hand-tuned pitch/yaw angles left
  the frog's arm tips buried in the pad with the feet floating apart.
  Same for welding: a foot paddle only unions onto its leg if its root
  tucks INSIDE the leg mass (ankle up under the thigh lump); a
  surface-adjacent root leaves a visibly separate floating part.
- Fins/appendages on such a body: thin box + `c.lattice_deform` fan/rake +
  2x `catmull_clark` (one level per MCP call, no iterations param), forked
  with one CSG cylinder difference; small paired fins are pooled `sweep`
  blades. Translucent fin material: alpha_blend opacity ~0.9-0.95 - at 0.85
  a backlit fin washes out white against the sky.
- Graph-mesh materials survive scene save/load since 2026-08-10 late
  (exported via extra_materials; older .glb files saved before the fix
  lack the material entirely - re-save from a fresh build).
- **Graph-body UVs are usable but rough** (fish, 2026-08-10): after the
  seam-aware CC interpolation fix and the coarse box-subdivisions pipeline,
  the cage's per-face UVs survive as a few large coherent tiles - good
  enough for an even scale pattern, though tile scale varies (denser pattern
  where tiles compress) and some tiles run past [0, 1]. TWO requirements:
  (a) **wrap=repeat on the material slots** - the clamp default smears
  out-of-range UV regions into stripe bands
  (`bind_material_texture(..., wrap="repeat")`); (b) coarse cell counts -
  16 cells/tile aliased into moire stripes at fish size, 6 reads as scales.
  A proper per-axis UV control is still future work
  (doc/geometry-graph-attribute-projection.md `project_attribute` node).
- Scales texture recipe (LANDED in creation 18, 2026-08-10): `shape`
  (circle, edge 1.0) -> `ensure_rgba` -> two `transform` repeats (scale
  1/cells, second offset by half a cell) -> `blend` lighten (=max) = quincunx
  scallop height; -> `greyscale` -> `colorize` (+ soft-light fbm mottle,
  amount ~0.2) for albedo; same field -> `normal_map` in a SECOND graph for
  the normal slot (one output per Graph_texture; creation 3's bricks_normal
  is the binding pattern). Both slots bound wrap="repeat".
