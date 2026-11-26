# Level kit shared points

Status: in progress

The rule of [../editor/level_kit_shapes.md](../editor/level_kit_shapes.md)
"Shared construction" ("Shared points"): where two parts of a shape's surface
meet, they are one solid sharing the vertices of the join. A review of every
shape generator (2026-11-26) found the joins below that dodged the mesh
checks' "two vertices share position" rule by overlapping, insetting or
capping instead.

## Fixed, kept as former builds

`nature.palm`, `nature.palm_clump`, `harbour.quay_wall`, `prop.market_stall`,
`prop.well`, `light.fire`, `industrial.vent`, `harbour.boat` and
`machine.spring`, each beside its `<id>_old` former build ("Former builds").

1. The user compares the pairs in the gallery scene
   (`scripts/level_kit_former_builds_gallery.py`).
2. Then the former builds are removed: the `add_former_build()` calls and
   the `Kit_build::former` branches of the generators,
   `Kit_leaf_section::folded_split`, `kit_shape_rows_former_builds.cpp`, the
   `_old` rows of `test_kit_collision.cpp`, the gallery script and the
   "Former builds" entry of the shapes reference.

## Not yet fixed

Each needs more than a local change; the user picks which to do.

- `nature.basalt_columns` (`kit_shapes_nature_rocks.cpp`, "Joints"): each
  column stands 2 - 5 cm in from its cell, slots between all columns. The
  joints are part of the shape's look and its one part per column
  (`kit_shape_rows_starter.cpp` components); sharing the lattice corners
  makes it one solid with walls split where three heights meet. User
  decision: keep the joints or close them.
- `architecture.tower`, battlement top (`kit_shapes_medieval.cpp`): the
  merlons are prisms 0.02 inside both parapet faces, a ledge on every merlon.
  Fix: the merlon ends in the parapet's plan ring, its faces raised under the
  merlons.
- `architecture.roof` (`kit_shapes_roof.cpp`): every tile course stops 0.02 m
  short of the next (`pull_back`), a slit down to the slab across each
  course; the body stands 0.02 inside the wall lines; the hip caps are
  separate beams with wedge overlaps and a notch at every joint. Fix: one
  sawtooth solid per slope, the body sharing the slab's underside, one tube
  along each hip.
- `architecture.entablature`, triglyph frieze (`kit_shapes_classical.cpp`,
  `entablature_relief`): the bars end 5 mm and the regula 2 mm behind the
  taenia's face.
- `machine.nut`, cap form (`kit_shapes_machine_01_08.cpp`): the dome's hole
  is 0.98 of the bore, a step inside the bore.
- `industrial.container` (`kit_shapes_extra_25_32.cpp`): the frame's rails
  are recessed about 1.2 cm from the posts' outer faces.
- `architecture.stairs`, landing forms (`kit_shapes_build_17_20.cpp`,
  `get_wall_lap`): the flight and landing walls on one line overlap with
  coplanar faces; hidden inside a pier.
- `architecture.stone_lantern` (`kit_shapes_build_21_24.cpp`): the window
  panels stop at 0.97 of the corner distance; hidden by the corner posts.

Borderline, left as they are unless the user asks: the signpost boards 4 mm
off the post, the shoji screens 6 mm apart, the amphora handle's elbow, the
rope bridge's loose plank, the cart's board insets, the tower base's
batter minimum, the land mass bench ring where the bench is 0, the torii's
lower lintel, the roof's hip gaps under the caps, the column capital's first
ring, the follower's sleeve bore, the dune's flat crest.
