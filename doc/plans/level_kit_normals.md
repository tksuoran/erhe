# Level kit normals

Status: in progress

Curved parts of a kit shape shade with curved normals; an edge is sharp only
where the design has one. The tools for this are described in
[../editor/level_kit_shapes.md](../editor/level_kit_shapes.md) "Hard edges"
and "Shading", and the crease smoothing in
[../editor/level_tools.md](../editor/level_tools.md) "Kit brushes". This plan
holds the shapes still to fix.

## The tools, in short

- A shape's `normal_style` with its `hard_edge_angle_degrees`: edges sharper
  than the angle stay hard. Enough where the shape's histogram of facet
  angles has a gap between its curves and its designed edges.
- `Kit_mesh_builder::add_hard_edge()`, `add_hard_loop()`: a designed edge
  that falls inside the curves' angle range (a folded leaf's midrib, a thread
  crest). Around a vertex of a hard edge, only hard edges split the normals:
  mark every designed edge that meets such a vertex (caps, rims, start faces),
  not just the one in question.
- `Kit_mesh_builder::set_smooth_from()`: a solid that shades round whatever
  its angles (a lumpy clump, a tube of four or five sides).
- Different slots never share normals: parts are built per slot.
- `make_shading_parameter(Kit_shading::smooth)` appended to the shape's own
  parameters gives the user the faceted look back. Saved pieces without the
  parameter read the default.

## Fixed

Committed 2026-11-26: the palm, palm clump, palm shrub and leaf plant; the
boulder cluster and stepping stone; the tree, bush, succulent, grass tuft and
bamboo; the eighteen machine parts. Every shape of these but the four palm
family shapes takes `shading`; the rocks `nature.rock`, `nature.rock_spire`,
`nature.rock_arch` and `nature.basalt_columns` take it at default `faceted`.

## Remaining

In order:

1. Curved props with a clear angle gap: smooth with hard edges at 45 - 50
   degrees and `shading` (default smooth): `prop.barrel`, `prop.pot`,
   `prop.well` (and `prop.well_old`), `prop.cart` (wheels),
   `harbour.lighthouse`, `harbour.buoy`, `harbour.mooring`,
   `industrial.tank`, `industrial.pipe`, `industrial.antenna`,
   `light.lamp_post`, `light.lantern`, `nature.log`,
   `architecture.spiral_stairs` (the central column),
   `architecture.railing` (the balusters). Check each render: a profile
   corner inside the band, like the bolt's thread, needs hard edges.
2. Shapes whose designed edges fall inside the curves' band, needing hard
   edges: `harbour.boat` (and `harbour.boat_old`: the gunwale, keel and
   transom sharp, the hull smooth), `nature.shell` (scallop ribs, starfish,
   conch, `kit_shapes_build_21_24.cpp`), `architecture.stone_lantern`
   (turned and carved parts), `light.fire` (and `light.fire_old`: logs and
   stones), `light.garland` and `traversal.rope_bridge` (four-sided rope
   tubes: `set_smooth_from()` on the ropes, the planks and posts sharp).
3. `shading` for `nature.palm`, `nature.palm_clump`, `nature.palm_shrub` and
   `nature.leaf_plant` (smooth already, no parameter yet).

## For the user to decide

- Whether `nature.rock`, `nature.rock_spire`, `nature.rock_arch` and
  `nature.basalt_columns` default to smooth (the user deferred it on
  2026-11-26, "other rocks decide later"); basalt columns are hexagons, so
  smooth makes them round.
- Whether `nature.cliff` and `nature.land_mass` take `shading`, with the same
  decision.

## Verification

Per batch (`AGENTS.md` "When to run the slow test suites"):

- The angle survey finds the curved shapes and the angle gaps: a temporary
  gtest in `editor_level_tests` that generates each shape at its defaults
  (`kit_test::generate(defaults(shape))`), measures the angle between the
  facet normals across every edge two facets share, and writes a histogram
  in 10 degree bins per shape to a file. Remove it before committing.
- Renders: `py -3 scripts/level_shape_render.py --editor
  build_vs2026_vulkan_headless/bin/Debug/editor.exe --only <id> --tag before
  --sun 3 --ambient 0.25` with the binary before the change, then rebuild
  and render `--tag after`; `--parameters '{"shading": "faceted"}'` checks
  the parameter. `py -3 scripts/level_shape_contact_sheet.py --tags before
  after --out logs/<name>.png <ids>` puts the pairs on one image;
  `--crop 300 300 --scale 1.2` zooms in.
- Tests: `editor_level_tests --gtest_filter=Kit_*` (all kit suites, a few
  minutes in Debug). `Kit_shading.shapes_pick_faceted_or_smooth` lists every
  shape that takes `shading`: add the new ids to it.
- Docs: each fixed group in `doc/editor/level_kit_shapes.md` "Shading", and
  a shape section that says "faceted" is updated.

## Traps

- Render the "before" images with the editor binary built before the edit;
  a rebuild in between makes both sides "after".
- A script that edits a shape definition by searching for its id must find
  `shape.id = "<id>"`, not an earlier mention of the id (a bamboo edit once
  changed `architecture.stairs`). Check the changed lines' shape ids before
  committing.
- `sed -i` in Git Bash rewrites a whole CRLF file with LF; check
  `git diff --stat` after it and restore a file that shows no real change.
- A former build (`<id>_old`) copies its shape's parameters and normal style.
