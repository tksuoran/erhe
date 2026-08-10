# Geometry-graph sculpting: smooth organic bodies

Building a smooth organic solid (a fish body) as a live geometry graph via
MCP. Creation 18 (fish) is the reference implementation.

- **Pipeline**: `c.geometry_graph(name)` then `box -> lattice -> subdivide ->
  output` (`g.add` + `g.chain`). Box cage sized to the body's bounding
  proportions with steps ~[8,4,4]; lattice sculpts the cage; `subdivide`
  (mode 0 = catmull_clark, iterations 3) delivers the fair surface. Bind
  with `c.bind_node_mesh(node_name, graph_name)`, then `move_node` - the
  bound node appears at the origin.
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
- Fins/appendages on such a body: thin box + `c.lattice_deform` fan/rake +
  2x `catmull_clark` (one level per MCP call, no iterations param), forked
  with one CSG cylinder difference; small paired fins are pooled `sweep`
  blades. Translucent fin material: alpha_blend opacity ~0.9-0.95 - at 0.85
  a backlit fin washes out white against the sky.
- **Graph-mesh materials do not survive scene save/load** (SKILL.md open
  bugs): screenshot from the live build, not a reloaded glb.
- **Graph-body UVs are unusable for texturing** (probed 2026-08-10 on the
  fish): the box cage's per-face UVs survive lattice + subdivision but
  collapse - most of the deformed surface maps to a tiny UV patch (renders
  as flat color) with dense stripe bands at the old box-face seams. A bound
  Graph_texture DOES sample and re-evaluate live (albedo + normal slots via
  `set_material_texture_source`), so texturing is blocked ONLY on UVs.
  Fix options: (a) a geometry-graph UV node - either Geogram atlas like the
  `generate_texture_coordinates` op, or a simple cylindrical/box projection
  (cylindrical along the body axis suits fish-like bodies); (b) workaround
  today: realize the graph output to a static mesh, then run
  `generate_texture_coordinates` once - loses live graph editing.
- Scales texture recipe (validated compose, blocked on body UVs): `shape`
  (circle, edge 1.0) -> `ensure_rgba` -> two `transform` repeats (scale
  1/cells, second offset by half a cell) -> `blend` lighten (=max) = quincunx
  scallop height; -> `colorize` for albedo, -> `greyscale` -> `normal_map`
  in a SECOND graph for the normal slot (one output per Graph_texture;
  creation 3's bricks_normal is the binding pattern).
