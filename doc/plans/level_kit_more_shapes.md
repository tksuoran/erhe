# Level kit: more shapes

Status: proposed

The shape list for step 62 of `doc/plans/level_tools_implementation.md`
("Expand the kit with more shapes"), chosen by the orchestrator within
rulings U34 and U36 of `doc/plans/level_tools.md`; the user reviews the
built shapes. It covers the user's five categories and the unbuilt shapes
of R4.3 and R4.4. Every shape joins an existing kit (U34), `nature` or
`architecture`; the new kits are step 63's
(`doc/plans/level_kit_extra_shapes.md`, "the extra shapes document"
below). This plan extends `doc/editor/level_tools.md`.

## Conventions of the tables

"Conventions of the tables" of the extra shapes document holds here too:
the unlisted `seed` and `segments`, lengths in modules (m) or wall heights
(wh), bold for the default derive mask, "fixed" for what walkability
depends on, the Body, Layouts, Geometry and Size columns, and its
prerequisites P1 - P5 by name. In addition:

- Slots: `sand` and `roof` are new slot names (Q1); `wood`, `water` and
  `glow` are those of P3.
- Geometry: `leaf` is the swept blade helper of Q2, `ring` the arch ring
  of Q3.
- Every nature shape is seeded (`Kit_randomness::seeded`); an
  architecture shape draws nothing from its seed unless it has a
  parameter in bold.

## Categories

36 shapes: 20 in `nature`, 16 in `architecture`.

- Rock formations (6). "Diverse sizes" come from the size parameters and
  their derive ranges; the forms differ in kind (a wall of rock, a heap, a
  tower, an arch, columns, a flat stone), so each is a shape.
- Tropical plants (5). Two leaf families cover the large-leaved and the
  succulent plants by a form parameter; bamboo and grass are canes and
  blades, different in kind. R4.4's blocky tree joins here as the plain
  tree family beside them.
- Palm trees (3). One family per growth habit: a single trunk, a clump of
  stems, a trunkless crown. The species is a parameter of each, since
  species of one habit differ by trunk line, frond kind and proportions,
  not by construction.
- Beach (6). Natural shapes only (U34): the sand forms, what the sea
  leaves on them, and the water itself. `nature.water` moves here from the
  extra shapes document, because a shore tile and a dune read as a beach
  only against a water surface.
- Architecture, common modules (6). The unbuilt R4.3 shapes that every
  culture uses, each with a style or form parameter where cultures differ
  in outline only (a window head, a roof form, a railing pattern).
- Architecture, cultural modules (10). Three cultures whose modules read
  at kit scale by silhouette alone, each a handful of shapes on the built
  module sizes and connectors: Classical Mediterranean (column, beam and
  pediment are the whole style, and they stack by base and top), Medieval
  European (crenellated walls and towers extend the built wall and arch
  directly), Japanese timber (post-and-beam bays, a gate and a curved roof
  contrast the two stone styles with a wood one). Each shape carries its
  culture as a tag (R4.6).

## R4.3 and R4.4 shapes

Where each unbuilt shape of R4.3 and R4.4 goes.

| Requirement shape | Here |
|---|---|
| R4.3 wall with window opening | `architecture.window_wall` |
| R4.3 wall corner and T-junction | left out: wall ends join by connector snap and overlap is accepted (U20); a square `architecture.pillar` of the wall's thickness is the corner post |
| R4.3 roof | `architecture.roof` |
| R4.3 stairs with side walls, L and U stairs with landing | `architecture.stairs` |
| R4.3 spiral stairs | `architecture.spiral_stairs` (Medieval) |
| R4.3 railing and fence run | `architecture.railing` |
| R4.3 bridge deck | `architecture.bridge_deck` |
| R4.3 platform | `architecture.stepped_platform` with one tier |
| R4.3 door frame | left out: `architecture.door_wall` at its minimum length and `architecture.torii` frame an opening |
| R4.3 battlement run | `architecture.battlement` (Medieval) |
| R4.4 boulder cluster, cliff segment, stepping stone | `nature.boulder_cluster`, `nature.cliff`, `nature.stepping_stone` |
| R4.4 ground patch, log and stump | `nature.ground_patch`, `nature.log` (Beach) |
| R4.4 blocky tree family, grass tuft | `nature.blocky_tree`, `nature.grass_tuft` (Tropical plants) |

## Inventory

### Rock formations (kit `nature`)

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `nature.cliff` | Cliff segment along X: a rough face toward +Z, flat ends, back and top, so segments join in a run and the top is walkable | length (4 m, 2 - 16), **height** (1 - 4 wh), depth (2 m), strata (`massive`, `layered`, `blocky`), **overhang** (-0.2 - 0.15 of the height), roughness, ledges (0 - 3) | rock, grass (top) | start, end, base, top | mesh | path, line | grid of jittered face vertices by `Kit_random`, polygon for the flat sides | L |
| `nature.boulder_cluster` | Several rocks of the `nature.rock` hull forms heaped in a round footprint | **radius** (1 - 4 m), **count** (3 - 9), size_spread (0.3 - 0.8), form (`angular`, `worn`, `mixed`) | rock | none | mesh | scatter | hull and chamfer, as `nature.rock` | M |
| `nature.rock_spire` | Standing rock family: pinnacle, sea stack, hoodoo with a cap, mesa with a flat top | form (`pinnacle`, `stack`, `hoodoo`, `mesa`), **height** (1 - 6 wh), **radius** (0.5 - 4 m), taper, **lean** (0 - 8 degrees), roughness | rock | base | mesh | scatter, ring | blob rings | M |
| `nature.rock_arch` | Natural arch across X with a walkable opening and a rough ring | span (3 m, fixed minimum 2), rise (1 wh, fixed minimum), **thickness** (0.8 - 2 meters), depth, roughness | rock | start, end, base | mesh | line | ring with jittered section | M |
| `nature.basalt_columns` | Cluster of hexagonal columns of seeded heights with flat tops | **radius** (1 - 4 m), column_width (0.5 m), height (1 wh), **height_spread** (0.1 - 0.6), step (the height quantum; at the agent's climb the tops are stairs) | rock | base | mesh | scatter, line | prism per column | S |
| `nature.stepping_stone` | Flat-topped stone, walkable | **diameter** (0.6 - 1.5 m), thickness (0.2 - 0.5 meters), sides (5 - 9) | rock | base, top | hull | path, scatter, line | hull of a jittered ring, flat top polygon | S |

### Tropical plants (kit `nature`)

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `nature.leaf_plant` | Rosette of large leaves on stalks; the tree fern stands on a trunk | form (`banana`, `taro`, `strelitzia`, `fern`, `tree_fern`), **height** (0.4 - 4 meters), **leaves** (4 - 12), **droop** (0.1 - 0.7), leaf_width | foliage, bark | none | none | scatter | leaf, prism for the trunk | M |
| `nature.succulent` | Rosette of thick pointed leaves; the yucca stands on a trunk | form (`agave`, `aloe`, `yucca`), **size** (0.4 - 2.5 meters), **leaves** (8 - 24), spread (20 - 80 degrees) | foliage, bark | none | hull | scatter | leaf with a thick section | S |
| `nature.bamboo` | Clump of jointed canes with leaf tufts near the tops | **height** (1 - 4 wh), **culms** (3 - 16), radius (0.5 m), **lean** (0 - 10 degrees), cane_width | bark, foliage | none | mesh | scatter, line, path | sweep per cane, leaf | M |
| `nature.grass_tuft` | Tuft of blades | form (`grass`, `reed`, `pampas`), **height** (0.2 - 2 meters), **blades** (6 - 24), spread | grass | none | none | scatter | leaf with a thin section | S |
| `nature.blocky_tree` | Tree of boxes: a square trunk and one to four canopy blocks | form (`cube`, `tiers`, `cluster`), **trunk_height** (0.4 - 0.8 wh), **canopy_size** (1.5 - 3 m), **blocks** (1 - 4) | bark, foliage | none | mesh | scatter, line, grid | box | S |

### Palm trees (kit `nature`)

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `nature.palm` | Single-trunk palm; the species sets the trunk line and the crown kind | species (`coconut`: curved slender trunk, feather fronds, nuts; `date`: thick trunk with frond stubs; `royal`: straight column with a crownshaft; `fan`: palmate leaves, a skirt of dead ones; `bottle`: swollen trunk), **height** (1 - 5 wh), **bend** (0 - 0.35 of the height), **fronds** (8 - 20), frond_length, **droop** | bark, foliage | none | mesh | scatter, line, path | sweep for the trunk, leaf; a fan leaf is a polygon fan | L |
| `nature.palm_clump` | Several slender stems from one root, each with a small crown | species (`areca`, `rhapis`, `nipa`), **stems** (3 - 9), **height** (0.6 - 2.5 wh), spread, fronds per stem (4 - 8) | bark, foliage | none | mesh | scatter | the trunk and crown of `nature.palm` | S |
| `nature.palm_shrub` | Trunkless crown at the ground | species (`palmetto`: fan leaves; `cycad`: stiff feather fronds on a short stump; `seedling`), **size** (0.6 - 2.5 meters), **fronds** (5 - 16) | foliage, bark | none | hull | scatter | the crown of `nature.palm` | S |

### Beach (kit `nature`)

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `nature.shore` | Beach tile along X: sand sloping from its back top edge down toward +Z to below the water level, with a seeded undulation; flat ends so tiles join | length (4 m), run (6 m), rise (1 m, fixed maximum slope), **undulation** (0 - 0.15 meters), berm (0 - 0.5 of the run, the flat part at the top) | sand | start, end, top (back edge), base | mesh | path, line | grid of height samples, polygon for the flat sides | M |
| `nature.dune` | Elongated sand mound with a gentle windward and a steeper lee side | **length** (4 - 12 m), **width** (2 - 6 m), **height** (0.3 - 0.7 of the fixed walkable limit), asymmetry (0 - 0.6) | sand | none | hull | scatter, line | blob rings scaled along X, as `nature.hill` | S |
| `nature.ground_patch` | Thin irregular patch that lies on other ground: sand, earth, grass | **size** (1 - 6 m), thickness (0.05 meters), **irregularity** (0.1 - 0.5), dome (0 - 0.1 of the size) | earth | none | hull | scatter | blob with a flat cap | S |
| `nature.log` | Dead wood family | form (`log`, `stump`, `driftwood` with branch stubs), **length** (0.5 - 4 meters), **width** (0.2 - 0.8 meters), **bend**, stubs (0 - 4) | bark | base (stump: base, top) | hull | scatter | prism, sweep for a bent log | M |
| `nature.shell` | Sea life left on the sand | form (`conch`, `scallop`, `starfish`), **size** (0.1 - 0.6 meters), ribs (5 - 12) | stone | none | none | scatter | sweep along a tapered helix, polygon fan | M |
| `nature.water` | Flat rectangular water surface at y = 0, a stated surface (R2.5) | width, depth (8 m each, 1 - 64 m), subdivisions (1 - 32 per side, fixed, default 1) | water | left, right, front, back (edge centers) | none | grid | polygon grid | S |

The water piece is placed at the water level and other pieces pass through
it (overlap is accepted, U20): `nature.shore` slopes down under it, the
shoreline being where the two meet; a `nature.cliff` or a
`nature.rock_spire` stands in it; step 63's quay wall and pier piles stand
on the bed below and rise through it, and its boat and buoy sit on it by
snap to surface. Patches tile by a grid layout or by their edge
connectors. The subdivisions give vertices for later vertex effects.

### Architecture, common modules (kit `architecture`)

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `architecture.window_wall` | `architecture.wall` with a window opening | length, height, thickness (the wall's), opening_width (1 meters), opening_height (1.2 meters), sill_height (0.9 meters), head (`flat`, `round`, `pointed`) | wall | the wall's six | mesh | line, path | ring for the head, polygon | M |
| `architecture.roof` | Roof over a rectangle, closed below | form (`gable`, `hip`, `shed`, `pyramid`), width (4 m), depth (4 m), pitch (15 - 60 degrees), overhang (0 - 0.6 meters), curve (0 - 0.3: the eaves turn up, the Japanese roof) | roof, trim (gable ends) | base, left, right, front, back | mesh | line, grid | polygon; a curved roof is rows of polygon strips | M |
| `architecture.stairs` | Stairs with side walls and landings | form (`walled`, `l_landing`, `u_landing`), rise (1 wh), width (1 m), step_rise_limit (fixed), wall_height (0 - 1.2 meters) | stone, trim | bottom, top, base | mesh | line | box per step, as `basic.stairs` | M |
| `architecture.railing` | Railing or fence run on posts | length (2 m), height (1 meters), post_spacing, style (`rail`, `picket`, `balustrade`, `lattice`) | trim | start, end, base | mesh | path, line | box, prism, beam (P4) | M |
| `architecture.bridge_deck` | Walkable deck between its two ends | length (4 m), width (2 m, fixed), thickness, supports (`none`, `piers`, `arched`), parapet (0 - 1 meters) | floor, stone | start, end, base | mesh | path, line | box, ring for arched supports | M |
| `architecture.stepped_platform` | Tiers of slabs, each inset from the one below: a platform, a stylobate, a stepped pyramid | width, depth (4 m), tiers (1 - 9), tier_rise (0.2 meters; at most the agent's climb every side is stairs), tier_inset (0.3 meters) | stone | base, top, front, back, left, right | mesh | grid | box per tier | S |

### Architecture, cultural modules (kit `architecture`)

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `architecture.column` | Classical: column with base, tapered fluted shaft and capital | order (`doric`, `ionic`, `corinthian`), height (1 wh), width (0.5 m), flutes (0, 12 - 24), taper (0 - 0.2) | trim | base, top | hull | line, grid, ring | blob rings as a lathe with a star section for flutes, box for the abacus | M |
| `architecture.entablature` | Classical: beam run of architrave, frieze and cornice that rests on column tops | length (2 m), height (0.25 wh), depth (0.5 m), cornice (0 - 0.3 meters) | trim | start, end, base, top | hull | line, path | profile polygon extruded along X | S |
| `architecture.pediment` | Classical: triangular gable front with a raking cornice | width (4 m), pitch (12 - 30 degrees), depth (0.5 m), recess (0 - 0.2 meters) | trim, wall (the recessed field) | base, start, end | mesh | line | polygon | S |
| `architecture.battlement` | Medieval: parapet run of merlons and crenels on a wall top | length (2 m), height (0.9 meters), thickness (the wall's default), merlon_width, crenel_width, merlon (`square`, `swallowtail`, `pointed`) | wall | start, end, base | mesh | line, path | box, polygon | S |
| `architecture.tower` | Medieval: round or square tower segment with a battered foot and a doorway; hollow, the top a walkable floor | profile (`round`, `square`), width (3 m), height (1.5 wh), batter (0 - 8 degrees), wall (0.4 meters), opening_width, opening_height (fixed minimums) | wall, floor | base, top, opening_front | mesh | line | polygon rings, ring for the doorway | L |
| `architecture.spiral_stairs` | Medieval: spiral stairs around a newel | rise (1 wh), radius (1.25 m, fixed minimum), step_rise_limit (fixed), turn (`left`, `right`), newel_width | stone | bottom, top, base | mesh | - | polygon wedge per step, prism for the newel | M |
| `architecture.buttress` | Medieval: stepped buttress against a wall face | height (1 wh), depth (1 m), width (0.5 m), steps (1 - 4) | stone | back, base | hull | line, path | profile polygon extruded along X | S |
| `architecture.torii` | Japanese: gate of two posts, a tie beam and a curved top beam | width (3 m), height (1.2 wh), **curve** (0 - 0.15 of the width), post_width, lean (0 - 4 degrees) | wood | base, opening_front, opening_back | mesh | path, line | prism, beam (P4), polygon strips for the curved beam | M |
| `architecture.timber_bay` | Japanese: one bay of a post-and-beam wall with its infill | length (2 m), height (1 wh), post_width (0.15 meters), infill (`open`, `shoji`, `plaster`, `half`), rails (0 - 3) | wood, wall | the wall's six | mesh | line, path | box | M |
| `architecture.stone_lantern` | Japanese: stone lantern of base, shaft, light box and cap; one point light | height (1.5 meters), width, cap (`square`, `hexagonal`), **cap_curve** | stone, glow | base | hull | path, line | prism, blob rings | M |

The walkable shapes hold R2.10 at every derived value: each of
`nature.cliff` (top), `nature.rock_arch`, `nature.shore`, `nature.dune`,
`architecture.stairs`, `architecture.spiral_stairs`,
`architecture.bridge_deck`, `architecture.tower` and
`architecture.window_wall` has its check or fixed parameters. The
classical modules join by base and top: column on stepped platform,
entablature on columns, pediment on entablature, at the module spacing of
a line layout of columns.

## Prerequisites

P1 - P5 are defined in the extra shapes document; Q1 - Q3 are this step's
own. The first commit holds P1 and Q1; the others land in the commit of
the first shape that needs them.

- P1 (triangle ceiling): before the first shape. The cliff, the palms, the
  tower and the boulder cluster pass 500 triangles at their defaults.
- P3 (slots and palette), in part: `wood` with `architecture.torii`,
  `water` and Kit Water with `nature.water`, `glow` and Kit Glow with
  `architecture.stone_lantern`.
- P4 (builder helpers): `add_mesh()` with `nature.palm`, the first shape
  that joins a sweep to other parts; `add_beam()` with
  `architecture.torii`.
- P5 (pieces without a body): with `nature.water`. `nature.leaf_plant`,
  `nature.grass_tuft` and `nature.shell` use it too: soft or small
  dressing a walker passes through.
- P2 (light on a piece): with `architecture.stone_lantern`, the only lit
  shape here and the last commit, so step 63's lights find P2 built.
- Q1 Slots `sand` and `roof` (S): `sand` with Kit Sand (alternatives Kit
  Earth, Kit Rock), `roof` with Kit Roof Tile (alternatives Kit Wood, Kit
  Stone); Kit Shell as an alternative of `stone`, Kit Sand as an
  alternative of `earth`.
- Q2 Leaf helper (M): one function that adds a leaf, frond or blade to the
  builder: a section (flat, folded or thick) swept by `make_sweep()` along
  a bent spine with a taper to a point, taken in by `add_mesh()`; a
  feather frond is a midrib with leaflet blades. Built with `nature.palm`;
  every `leaf` in the tables calls it.
- Q3 Arch ring heads (S): `make_arch_points()` of
  `kit_shapes_architecture.cpp` shared through `kit_shapes.hpp` with a
  head form (`round`, `pointed`) and an optional seeded jitter. Built with
  `architecture.window_wall`.

## Build order

One commit per line, each with its checks in
`scripts/level_tools_verify.py` and its entry in
`doc/editor/level_tools.md`. The first ten put one strong silhouette of
every category in front of the user; the rest follow by category.

1. P1 and Q1.
2. `nature.cliff`.
3. `nature.palm` with P4 `add_mesh()` and Q2.
4. `nature.shore` and `nature.water` with P5.
5. `architecture.column` and `architecture.entablature`.
6. `nature.leaf_plant`.
7. `architecture.torii` with P4 `add_beam()` and the `wood` slot.
8. `architecture.battlement` and `architecture.tower`.
9. `nature.rock_spire` and `nature.basalt_columns`.
10. `architecture.roof`.
11. `nature.palm_clump` and `nature.palm_shrub`.
12. `nature.dune` and `nature.ground_patch`.
13. `architecture.window_wall` with Q3.
14. `architecture.pediment` and `architecture.stepped_platform`.
15. `nature.succulent` and `nature.grass_tuft`.
16. `nature.boulder_cluster` and `nature.stepping_stone`.
17. `architecture.timber_bay` and `architecture.railing`.
18. `nature.bamboo`.
19. `architecture.stairs` and `architecture.spiral_stairs`.
20. `nature.rock_arch`.
21. `architecture.bridge_deck` and `architecture.buttress`.
22. `nature.log` and `nature.shell`.
23. `nature.blocky_tree`.
24. `architecture.stone_lantern` with P2.
