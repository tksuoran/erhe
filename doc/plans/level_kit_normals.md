# Level kit normals

Status: blocked

Curved parts of a kit shape shade with curved normals; an edge is sharp only
where the design has one. The tools for this are described in
[../editor/level_kit_shapes.md](../editor/level_kit_shapes.md) "Hard edges"
and "Shading", and the crease smoothing in
[../editor/level_tools.md](../editor/level_tools.md) "Kit brushes". This plan
holds what waits for the user's decision.

## The tools, in short

- A shape's `normal_style` with its `hard_edge_angle_degrees` (by default
  smooth with hard edges above 50 degrees): edges sharper than the angle
  stay hard. Enough where the shape's histogram of facet
  angles has a gap between its curves and its designed edges.
- `Kit_mesh_builder::add_hard_edge()`, `add_hard_loop()`: a designed edge
  that falls inside the curves' angle range (a folded leaf's midrib, a thread
  crest). Around a vertex of a hard edge, only hard edges split the normals:
  mark every designed edge that meets such a vertex (caps, rims, start faces),
  not just the one in question.
- `Kit_mesh_builder::set_smooth_from()`: a solid that shades round whatever
  its angles (a lumpy clump, a tube of four or five sides).
- Different slots never share normals: parts are built per slot.
- `shape.default_shading` picks the default of the `shading` parameter that
  `Kit_shape_library` gives every shape; the other value gives the user the
  other look. Saved pieces without the parameter read the default.

## Done

Every shape takes `shading`; `doc/editor/level_kit_shapes.md` "Shading"
lists the defaults. Boxy shapes look the same either way and default to
faceted.

## For the user to decide

- Whether `nature.rock`, `nature.rock_spire`, `nature.rock_arch` and
  `nature.basalt_columns` default to smooth (the user deferred it on
  2026-11-26, "other rocks decide later"); basalt columns are hexagons, so
  smooth makes them round.
- Whether `nature.cliff` and `nature.land_mass` default to smooth. Both take
  `shading` at default faceted with the 80 degree crease; the cliff's smooth
  render shows a spike of stretched facets at one side edge, which faceted
  shading hides.
- Any other default the user's review of the shapes changes: each is one
  `shape.default_shading` line.

## Verification

Per batch (`AGENTS.md` "When to run the slow test suites"):

- The angle survey finds the curved shapes and the angle gaps: a temporary
  gtest in `editor_level_tests` that generates each shape at its defaults
  (`kit_test::generate(defaults(shape))`), measures the angle between the
  facet normals across every edge two facets share, and writes a histogram
  in 10 degree bins per shape to a file. Remove it before committing.
- Renders: `--parameters '{"shading": "faceted"}'` reproduces the look
  before smoothing, so one binary renders both sides:
  `py -3 scripts/level_shape_render.py --only <id> --tag smooth --sun 3
  --ambient 0.25 --parameters '{"shading": "smooth"}'`, then the same with
  `faceted`; `--detail x,y,z,distance` for a small shape.
  `py -3 scripts/level_shape_contact_sheet.py --tags faceted smooth --out
  logs/<name>.png <ids>` puts the pairs on one image; `--crop 300 300
  --scale 1.2` zooms in.
- Tests: `editor_level_tests --gtest_filter=Kit_*` (all kit suites, a few
  minutes in Debug); `Kit_shading.shapes_pick_faceted_or_smooth` checks the
  parameter on every shape and a few defaults by name.
- Docs: a changed default in the lists of `doc/editor/level_kit_shapes.md`
  "Shading".

## Traps

- A build started before an edit compiles whatever sources it reaches after
  the edit: edit only once the build has finished.
- A new parameter shifts the random value stream of
  `Kit_shape_random_values.pass_the_checks`, which then tries values it
  never tried before and can find an older defect (the pinched arched palm
  frond): fix the defect, never the stream.
- A script that edits a shape definition by searching for its id must find
  `shape.id = "<id>"`, not an earlier mention of the id (a bamboo edit once
  changed `architecture.stairs`). Check the changed lines' shape ids before
  committing.
- `sed -i` in Git Bash rewrites a whole CRLF file with LF; check
  `git diff --stat` after it and restore a file that shows no real change.
- A former build (`<id>_old`) copies its shape's normal style and default
  shading.
