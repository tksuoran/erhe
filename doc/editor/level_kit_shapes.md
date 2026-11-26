# Level kit shapes

Stability: experimental

The reference of the level kit's shape generators: the kits, the material
slots and the palette, the derive ranges, and per shape its id, parameters
(defaults and ranges), slots, connectors, body, mirror planes, tags and how
its mesh is built. What a shape is and how pieces are placed, derived,
replaced, grouped and lit is `doc/editor/level_tools.md` ("Shapes", "Derive
from seed", "Former shape ids", "Materials", "Groups", "Lights on pieces").
The open questions about the shapes are in `doc/plans/level_kit_more_shapes.md`,
`doc/plans/level_kit_extra_shapes.md` and
`doc/plans/level_kit_machine_elements.md`. The generators are in
`src/editor/level/kit_shapes_<group>.cpp`.

## Kits

`Kit_shape_library` (`kit_shape_library.cpp`) holds ten kits, each with a
triangle ceiling of 2000 (`Kit_definition::triangle_ceiling`, R2.6) that
binds at every in-range value: each shape's ranges and counts keep it within
the ceiling at its range maxima.

| Kit | Display name | Shapes |
|---|---|---|
| `basic` | Basic | "Basic kit" |
| `architecture` | Architecture | "Architecture kit" |
| `nature` | Nature | "Nature kit" |
| `prop` | Props | "Prop kit" |
| `ruin` | Ruins | "Ruin kit" |
| `traversal` | Traversal | "Traversal kit" |
| `harbour` | Harbour | "Harbour kit" |
| `light` | Lights | "Light kit" |
| `industrial` | Industrial | "Industrial kit" |
| `machine` | Machine Elements | "Machine kit" |

## Shared construction

- Topology from integers (R3.7), but for the level of detail ("Level of
  detail"), the one float that sets counts: the basic shapes, the door wall, pillar,
  arch, battlement, tower, rock, land mass, hill, tree, bush, rock spire, leaf
  plant and palm shrub, every shape of the prop, ruin, traversal, harbour, light, industrial and machine kits, and the
  succulent, grass tuft, boulder cluster, stepping stone, timber bay,
  bamboo, stairs with side walls, spiral stairs, rock arch, bridge
  deck, buttress, log, shell and blocky tree build their topology (vertex
  count, facets, their order, the slot of each facet) from their integer and
  enum values and integer draws alone; float values, float draws and the
  seed only move vertices. The integers taken from lengths are the step count
  of the stairs (`Kit_stairs_layout::step_count`, `basic.stairs`,
  `architecture.stairs`, `architecture.spiral_stairs`), a ladder's rungs
  (from `height` and `rung_spacing`), a pier's planks (from `length`) and
  bents (from `length` and `pile_spacing`), and a rope bridge's planks (from
  `span` and `plank_gap`). Three shapes take further counts from
  lengths: the battlement its bays (from `length`, `merlon_width` and
  `crenel_width`); the railing its bays (from `length` and `bay_length`);
  the tower its merlons (round, from `width`), its
  crenels per side and corbels between the corners (square, from `width`),
  its rows of slits (as many of `slit_rows` as fit between the cut's head,
  from `opening_height`, and the corbels or the top floor, from `height`),
  and its front slits and the wall rings' plan corners (those the doorway's
  jambs leave room for, from `opening_width` and `width`). The bridge deck,
  buttress, log, shell and blocky tree draw integers only to choose places
  (a side, a face), never a count.
- Slots at every form: a shape of the prop, ruin, traversal, harbour, light,
  industrial and machine kits, and the bridge deck, buttress, log, shell and
  blocky tree, have facets of every slot at every form, since a piece has a
  part for each slot.
- Origin: a shape with `Kit_origin_rule::base_contains_origin` has a base
  that surrounds the origin without being centered on it (among others the
  quay wall, the pipe, the table, the cart, the well, the stairs and the
  machine parts that rest on narrow lines).
- Arch heads: `make_kit_arch_head_points()` (`kit_shapes.hpp`, Q3 of
  `doc/plans/level_kit_more_shapes.md`) is the head of an opening: half an
  ellipse (`round`) or two arcs with their centers on the springing line that
  rise 1.5 half widths (`pointed`). The left half is the right half's mirror
  image to the bit, the feet and the crown are exact, and a pointed head
  lower than 1.2 of its half span is the head of that rise pressed down. It
  also serves a seeded jitter of the points along their radii. The window
  wall, the rock arch and the tunnel use it.
- Ropes: a rope (the garland's wire, the rope bridge's four ropes) is a chain
  of sheared boxes: a tube of four corners whose stretches share their
  rings, so its faces are planar along any curve.
- Lights: the stone lantern, the lamp post, the lantern, the fire, the buoy,
  the lighthouse and the garland carry light frames
  (`doc/editor/level_tools.md` "Lights on pieces"). "The kit's starting
  values" of a light are a warm white point light of 40 cd with a range of
  8 m.
- Shared points: where two parts of a surface meet (the halves of a folded
  leaf at its midrib and its tip, the two slopes of a ridge, a hull's sides at
  its stem, a hook and the coil it continues), they are one solid that shares
  the vertices of the join; the corner attributes (normals) stay per facet. A
  concave section is closed by convex pieces that share their edges
  (`Kit_mesh_builder::add_x_prism()`). Parts that only reach into one another
  (a post into a beam) stay separate closed solids.
- Former builds: a shape whose build was fixed keeps its former build for a
  side by side comparison as `<id>_old` ("<name> (old)", `add_former_build()`
  in `kit_shape_library.cpp`, the generator taking `Kit_build::former`), with
  no former ids and no foliage radius. `editor_level_tests` holds each to its
  shape's rows and to the triangle count and pins of the build before the fix
  (`kit_shape_rows_former_builds.cpp`).
  `scripts/level_kit_former_builds_gallery.py` builds a scene of every pair,
  the former build at -X beside the current one. The former builds are
  `nature.palm`, `nature.palm_clump` (fronds: two wedges crossing at the
  midrib, capped short of the tip), `harbour.quay_wall` (coping set in from
  the land face), `prop.market_stall` (a peaked awning of two boxes, an inset
  valance), `prop.well` (a gable of two boxes), `light.fire` (tripod heads
  apart), `industrial.vent` (a duct elbow of two boxes), `harbour.boat` (a
  rowboat's and a skiff's bow a narrow board wide) and `machine.spring` (hooks
  and legs separate from the coil). They are removed once compared
  (`doc/plans/open_items.md` "Work order").
- Water is no snap to surface target (`doc/editor/level_tools.md` "Surface
  placement"): a buoy and a boat stand on their base and are sunk by hand.

## Level of detail

Every shape takes the float parameter `detail` (`c_kit_detail_parameter`),
its last, which `Kit_shape_library` adds to every definition
(`make_detail_parameter()`): a ratio from 0.1 to 1, default 0.5, never
derived. At the default a shape builds with its own counts, so a piece's
mesh is the one it had before the parameter existed; lower values make fewer
triangles, higher values more. The default is left out of a piece's
canonical text, and a text without `detail` reads as the default
(`Kit_text_presence::unless_default`), so records written before the
parameter read and keep their mesh key.

A shape states the count parameters the detail scales in
`Kit_shape_definition::detail_counts`: each `Kit_detail_count` names an int
parameter, the dimensions of the mesh it resolves (1 for a ring's points, 2
for a grid's columns whose rows follow them) and a step. The count takes
(detail / 0.5) ^ (1 / dimensions) of its value, rounded to whole steps from
it (a step of 2 keeps the parity of a count whose odd values build
differently) and kept within the parameter's range
(`get_kit_detail_values()`). The library wraps such a shape's generator and
its other callbacks describing the mesh (walkability check, seam sides,
connectors, light frames, collision kind), so all of them see the scaled
counts. Parts the counts do not reach keep their triangles: the share is
the counts' share. A shape with no detail counts ignores `detail` and builds
one mesh at every value; its row is disabled ("This shape has one level of
detail").

The shapes with detail counts scale their `segments`: `basic.cylinder`,
`architecture.pillar`, `architecture.column`, `architecture.torii`,
`architecture.window_wall`, `architecture.tunnel`, `nature.land_mass`,
`nature.hill` (two dimensions), `nature.rock_spire` (step 2),
`nature.rock_arch`, `nature.log`, `nature.dune`, `nature.ground_patch`,
`prop.barrel`, `prop.pot`, `prop.cart`, `prop.well`, `harbour.pier`,
`harbour.mooring`, `harbour.buoy`, `harbour.lighthouse`, `light.lamp_post`,
`industrial.pipe`, `industrial.tank`, `ruin.broken_pillar` and
`ruin.broken_arch` (and `prop.well_old`). The other shapes, and the counts
fixed inside generators, are converted step by step
(`doc/plans/level_kit_detail.md`).

## Slots and palette

The palette (`Kit_shape_library::get_palette()`, base colors linear):

| Material | Base color | Roughness | Metallic | Emissive |
|---|---|---|---|---|
| Kit Stone | (0.50, 0.50, 0.47) | 0.85 | 0 | - |
| Kit Floor | (0.42, 0.34, 0.26) | 0.75 | 0 | - |
| Kit Plaster | (0.78, 0.72, 0.60) | 0.9 | 0 | - |
| Kit Trim | (0.60, 0.56, 0.50) | 0.8 | 0 | - |
| Kit Wood | (0.45, 0.29, 0.15) | 0.7 | 0 | - |
| Kit Dark Wood | (0.16, 0.09, 0.05) | 0.7 | 0 | - |
| Kit Vermilion | (0.72, 0.07, 0.03) | 0.55 | 0 | - |
| Kit Rock | (0.36, 0.35, 0.34) | 0.95 | 0 | - |
| Kit Earth | (0.36, 0.24, 0.14) | 0.95 | 0 | - |
| Kit Grass | (0.22, 0.42, 0.12) | 0.9 | 0 | - |
| Kit Foliage | (0.10, 0.32, 0.08) | 0.85 | 0 | - |
| Kit Autumn Foliage | (0.48, 0.22, 0.05) | 0.85 | 0 | - |
| Kit Bark | (0.22, 0.15, 0.09) | 0.95 | 0 | - |
| Kit Dark Rock | (0.13, 0.12, 0.11) | 0.95 | 0 | - |
| Kit Sand | (0.76, 0.62, 0.40) | 0.95 | 0 | - |
| Kit Shell | (0.85, 0.78, 0.70) | 0.6 | 0 | - |
| Kit Roof Tile | (0.52, 0.20, 0.12) | 0.8 | 0 | - |
| Kit Water | (0.04, 0.18, 0.30) | 0.1 | 0 | - |
| Kit Shallow Water | (0.10, 0.40, 0.42) | 0.1 | 0 | - |
| Kit Straw | (0.66, 0.55, 0.30) | 0.9 | 0 | - |
| Kit Bamboo | (0.30, 0.42, 0.10) | 0.55 | 0 | - |
| Kit Golden Bamboo | (0.68, 0.52, 0.16) | 0.55 | 0 | - |
| Kit Pale Wood | (0.62, 0.47, 0.28) | 0.8 | 0 | - |
| Kit Coral | (0.80, 0.30, 0.18) | 0.7 | 0 | - |
| Kit Glow | (1.00, 0.72, 0.36) | 0.4 | 0 | (2.20, 1.20, 0.35) |
| Kit Metal | (0.22, 0.23, 0.25) | 0.5 | 0.6 | - |
| Kit Rust | (0.32, 0.13, 0.06) | 0.85 | 0.4 | - |
| Kit Cloth | (0.70, 0.64, 0.52) | 0.95 | 0 | - |
| Kit Red Cloth | (0.55, 0.07, 0.06) | 0.95 | 0 | - |
| Kit Blue Cloth | (0.08, 0.17, 0.42) | 0.95 | 0 | - |
| Kit Clay | (0.58, 0.27, 0.15) | 0.8 | 0 | - |
| Kit Ember | (0.60, 0.18, 0.05) | 0.6 | 0 | (1.60, 0.45, 0.08) |
| Kit Steel | (0.70, 0.72, 0.75) | 0.35 | 0.6 | - |
| Kit Brass | (0.78, 0.58, 0.22) | 0.35 | 0.6 | - |
| Kit Leaf | (0.17, 0.53, 0.13) | 0.6 | 0 | - |
| Kit Young Leaf | (0.40, 0.70, 0.18) | 0.55 | 0 | - |
| Kit Autumn Leaf | (0.80, 0.37, 0.08) | 0.7 | 0 | - |
| Kit Grass Blade | (0.34, 0.65, 0.18) | 0.7 | 0 | - |

The last four are leaf materials (`doc/erhe/leaf_material.md`): the light
behind a leaf shows through it, tinted by its transmission color.

| Material | Transmission | Transmission color | Thickness |
|---|---|---|---|
| Kit Leaf | 0.40 | (0.45, 0.75, 0.12) | 0.06 m |
| Kit Young Leaf | 0.45 | (0.60, 0.85, 0.20) | 0.06 m |
| Kit Autumn Leaf | 0.40 | (0.95, 0.55, 0.12) | 0.06 m |
| Kit Grass Blade | 0.35 | (0.55, 0.80, 0.18) | 0.04 m |

A leaf material's base color is its opaque counterpart's divided by
`1 - transmission`, so a lit leaf reflects as much as the opaque material
does. The shapes' leaves are closed thin solids, so the thickness is over the
thickest leaf the shapes make (palm fronds and banana blades about 5 cm,
grass blades about 3.6 cm): transmitted light's shadow is looked up at the
far face. `make_palette_material()` makes a palette entry with a
transmission a leaf material, single sided, its back face copying the front.

Each slot name has a `Kit_slot_palette` (`get_slot_palettes()`,
`find_slot_palette()`): its default material and a few alternatives, by
palette name:

| Slot | Default | Alternatives |
|---|---|---|
| stone | Kit Stone | Kit Rock, Kit Plaster, Kit Wood, Kit Shell, Kit Clay |
| floor | Kit Floor | Kit Wood, Kit Stone, Kit Earth |
| wall | Kit Plaster | Kit Stone, Kit Wood, Kit Trim |
| trim | Kit Trim | Kit Wood, Kit Stone, Kit Plaster |
| rock | Kit Rock | Kit Stone, Kit Earth |
| earth | Kit Earth | Kit Grass, Kit Rock, Kit Stone, Kit Sand |
| grass | Kit Grass | Kit Earth, Kit Foliage |
| foliage | Kit Foliage | Kit Autumn Foliage, Kit Grass |
| leaf | Kit Leaf | Kit Young Leaf, Kit Autumn Leaf, Kit Foliage |
| young_leaf | Kit Young Leaf | Kit Leaf, Kit Autumn Leaf, Kit Grass |
| blade | Kit Grass Blade | Kit Leaf, Kit Autumn Leaf, Kit Grass, Kit Straw |
| bark | Kit Bark | Kit Wood, Kit Rock |
| cliff | Kit Dark Rock | Kit Rock, Kit Stone, Kit Sand |
| sand | Kit Sand | Kit Earth, Kit Rock |
| roof | Kit Roof Tile | Kit Wood, Kit Stone |
| water | Kit Water | Kit Shallow Water |
| wood | Kit Wood | Kit Vermilion, Kit Dark Wood, Kit Bark |
| shoot | Kit Grass | Kit Foliage, Kit Autumn Foliage |
| plume | Kit Straw | Kit Shell, Kit Bark, Kit Autumn Foliage |
| cane | Kit Bamboo | Kit Golden Bamboo, Kit Straw, Kit Dark Wood |
| sapwood | Kit Pale Wood | Kit Wood, Kit Straw, Kit Shell |
| heartwood | Kit Wood | Kit Dark Wood, Kit Bark, Kit Pale Wood |
| shell | Kit Shell | Kit Sand, Kit Stone, Kit Coral |
| coral | Kit Coral | Kit Shell, Kit Vermilion, Kit Sand |
| glow | Kit Glow | Kit Ember |
| metal | Kit Metal | Kit Rust, Kit Dark Wood, Kit Stone, Kit Steel, Kit Brass |
| cloth | Kit Cloth | Kit Red Cloth, Kit Blue Cloth, Kit Straw |
| steel | Kit Steel | Kit Brass, Kit Metal, Kit Rust |

## Derive ranges

The derivable parameters, their derive ranges and the default masks (masked
in bold) of the shapes whose sections below do not state them; every seed
and every segment count is fixed.

| Shape | Derivable (range) |
|---|---|
| basic.block | width, height, depth (0.5 - 3 modules, step 0.25) |
| basic.wedge | run (2 - 4 modules), rise (0.25 - 1.25 modules), width (1 - 3 modules), step 0.25 |
| basic.cylinder | radius (0.25 - 1.5 modules, step 0.05), height (0.5 - 3 modules, step 0.25) |
| basic.stairs | rise (0.25 - 1.5 wall heights, step 0.05), width (1 - 3 modules, step 0.25); step_rise_limit fixed |
| basic.floor_slab | width, depth (1 - 4 modules, step 0.5), thickness (0.1 - 0.4 m, step 0.05) |
| architecture.wall | length (1 - 6 modules, step 0.5), height (0.75 - 1.25 wall heights, step 0.05), thickness (0.15 - 0.4 m, step 0.05) |
| architecture.door_wall | length (2.5 - 6 modules, step 0.5), height (0.85 - 1.25 wall heights), thickness (0.15 - 0.4 m), opening_height (2.0 - 2.4 m), step 0.05; opening_width (0.9 - 1.6 m, step 0.1) |
| architecture.pillar | profile, width (0.3 - 1 modules), height (0.5 - 1.5 wall heights), step 0.05 |
| architecture.arch | span (2 - 4 modules, step 0.25), rise (0.8 - 1.2 wall heights), depth (0.3 - 1 modules), thickness (0.2 - 0.5 m), step 0.05 |
| architecture.column | order, height (0.8 - 1.6 wall heights, step 0.05), width (0.3 - 0.9 modules, step 0.05), flutes (12 - 24), taper (0 - 0.2, step 0.01) |
| architecture.entablature | length (1 - 4 modules, step 0.5), height (0.2 - 0.35 wall heights), cornice (0.05 - 0.3 m), step 0.01; depth (0.3 - 0.9 modules, step 0.05), frieze |
| architecture.torii | width (2.5 - 5 m, step 0.25), height (3.2 - 4.8 m, step 0.1), **curve** (0 - 0.15, step 0.01), post_width (0.2 - 0.45 m, step 0.01), lean (0 - 4 degrees, step 0.5) |
| architecture.battlement | length (1 - 6 modules, step 0.5), height (0.8 - 1.2 m), thickness (0.2 - 0.6 m), merlon_height (0.7 - 1.2 m), crenel_width (0.5 - 1 m), step 0.05; merlon_width (0.8 - 1.6 m, step 0.1), merlon |
| architecture.tower | profile, width (3 - 6 m, step 0.5), height (1.2 - 3 wall heights, step 0.1), batter (0 - 8 degrees, step 0.5), wall (0.3 - 0.6 m, step 0.05), top, slit_rows (0 - 2); opening_width and opening_height fixed |
| architecture.roof | form, width, depth (2 - 8 modules, step 0.5), pitch (20 - 45 degrees, step 5), overhang (0.1 - 0.5 m), curve (0 - 0.3), step 0.05 |
| architecture.window_wall | length (2.5 - 6 modules, step 0.5), height (0.85 - 1.25 wall heights), thickness (0.15 - 0.4 m), opening_height (1.0 - 1.3 m), sill_height (0.7 - 1.0 m), step 0.05; opening_width (0.8 - 1.6 m, step 0.1), head, mullions (0 - 2), transoms (0 - 1); segments fixed |
| architecture.pediment | width (2 - 8 modules, step 0.5), pitch (12 - 30 degrees, step 1), depth (0.3 - 0.9 modules, step 0.05), recess (0.05 - 0.2 m, step 0.01), dentils (6 - 12) |
| architecture.stepped_platform | width, depth (3 - 8 modules, step 0.5), tiers (1 - 5), tier_rise (0.15 - 0.35 m), tier_inset (0.3 - 0.5 m), step 0.05 |
| architecture.stone_lantern | height (1.2 - 2.1 m, step 0.1), width (0.5 - 0.8 m, step 0.05), cap (every value), **cap_curve** (0.2 - 0.9, step 0.05) |
| nature.rock | form, **size** (0.6 - 1.5 modules, step 0.05), roughness (0.25 - 0.8, step 0.01), pieces (1 - 4) |
| nature.land_mass | footprint (3 - 8 modules, step 0.5), height (0.5 - 2 modules, step 0.25), **erosion** (0.2 - 0.9, step 0.01) |
| nature.hill | radius (2.5 - 6 modules, step 0.5), **height** (0.5 - 0.9 modules, step 0.05), rocks (1 - 3) |
| nature.tree | **trunk_height** (0.4 - 0.8 wall heights, step 0.05), **canopy_size** (1.5 - 3 modules, step 0.1), species |
| nature.bush | **size** (0.6 - 1.4 modules, step 0.05), twigs (2 - 4) |
| nature.cliff | length (2 - 16 modules, step 0.5), **height** (1 - 4 wall heights, step 0.1), depth (1.5 - 3 modules, step 0.25), strata, **overhang** (-0.2 - 0.05, step 0.05), roughness (0.3 - 0.8, step 0.05), ledges (0 - 3) |
| nature.shore | length (2 - 8 modules), run (5 - 10 modules), step 1; **undulation** (0 - 0.15 m, step 0.01), berm (0 - 0.4, step 0.05); rise fixed |
| nature.water | width, depth (4 - 16 modules, step 1); subdivisions fixed |
| nature.palm | species, **height** (1 - 5 wall heights, step 0.1), **bend** (0 - 0.35, step 0.01), **fronds** (8 - 20), frond_length (3 - 5.5 m, step 0.1), **droop** (0.2 - 0.7, step 0.01) |
| nature.leaf_plant | form, **height** (0.4 - 4 m, step 0.1), **leaves** (4 - 12), **droop** (0.1 - 0.7, step 0.01), leaf_width (0.25 - 0.45, step 0.01) |
| nature.rock_spire | form, **height** (1 - 6 wall heights, step 0.1), **radius** (0.5 - 4 modules, step 0.1), taper (0.2 - 0.8, step 0.05), **lean** (0 - 8 degrees, step 0.5), roughness (0.3 - 0.9, step 0.05), strata (7 - 11) |
| nature.basalt_columns | **radius** (1 - 3 modules, step 0.25), column_width (0.4 - 0.8 modules), height (0.5 - 1.5 wall heights), **height_spread** (0.1 - 0.6), step 0.05; step fixed |
| nature.palm_clump | species, **stems** (3 - 9), **height** (0.6 - 1.6 wall heights, step 0.1), spread (0.2 - 0.8, step 0.05), fronds (4 - 8) |
| nature.palm_shrub | species, **size** (0.6 - 2.5 m, step 0.1), **fronds** (5 - 16) |
| nature.dune | **length** (9 - 15 m, step 0.5), **width** (4.5 - 7 m, step 0.25), **height** (0.5 - 0.85 m), asymmetry (0 - 0.4), step 0.05 |
| nature.ground_patch | **size** (1 - 6 modules, step 0.25), **irregularity** (0.1 - 0.5, step 0.01), dome (0 - 0.1, step 0.01); thickness fixed |
| nature.succulent | form, **size** (0.4 - 2.5 m, step 0.1), **leaves** (8 - 24), spread (20 - 80 degrees, step 5) |
| nature.grass_tuft | form, **height** (0.2 - 2 m, step 0.05), **blades** (8 - 24), spread (0.2 - 0.8, step 0.05) |
| nature.boulder_cluster | **radius** (1 - 4 modules, step 0.25), **count** (3 - 9), size_spread (0.3 - 0.8), roughness (0.3 - 0.9), step 0.05; form |
| nature.stepping_stone | **diameter** (0.6 - 1.5 m, step 0.05), thickness (0.2 - 0.4 m, step 0.05), sides (5 - 9) |

The basic and architecture shapes mask nothing by default but the torii's
curve and the stone lantern's cap curve. The ranges of the walkable shapes
keep them walkable at every seed at the default lengths (R2.10): a derived
wedge is at most 32 degrees steep, a derived hill's height is at most half
its radius, a derived door opening and arch opening pass the opening check,
the stairs' step rise limit and the shore's rise are never derived, a
derived shore is at most 40 degrees steep, a cliff's setbacks leave its top
walkable at every value, a derived torii's tie beam stays above the agent and
its posts apart, a derived tower keeps its doorway, inside and walkway (its
doorway is never derived), the basalt columns' step is never derived, a
derived dune's lee stays within 45 degrees and a derived ground patch's dome
within the agent's slope. A derived window's sill is above the agent's climb
at any level lengths (it is stated in meters), and its least length and
height hold every derived opening, so no derived value set is refused. The
torii, the tower, the dune and the window wall state the sizes the agent
bounds in meters, so their derived values are accepted and walkable at any
level lengths; six shapes (the wedge, stairs, floor slab, arch, land mass
and shore) scale theirs with the lengths and derive unwalkable values at
other lengths (`doc/plans/open_items.md` "Editor defects found in passing").

## Basic kit

`kit_shapes_basic.cpp`. Every basic shape has the slot `stone` (the floor
slab: `floor`), is faceted (the cylinder: smooth with hard edges), draws
nothing from its seed and masks nothing by default.

### Block

`basic.block` is a box with its origin at the center of its base that tiles
on the module grid. Parameters `width`, `height` and `depth` (1 module each,
0.05 - 64 m); connectors base, top, and front, back, left and right at the
centers of the base's edges; a box body; mirror planes x and z; every
side a seam side; tags `obstacle`, `walkable` and `tiles`. Six quads over
eight corners, 12 triangles.

### Wedge

`basic.wedge` is a ramp rising toward -Z from its front edge at +Z, its
origin at the center of its base. Parameters `run` (the slope's length along
Z, 2 modules), `rise` (the high end's height, 1 module) and `width` (1
module); connectors bottom (the front edge on the ground, facing +Z), top
(the high edge, facing -Z) and base; a convex hull body; mirror plane x; tags
`walkable` and `slope`. The triangular prism, five facets over six corners,
8 triangles.

### Cylinder

`basic.cylinder` is an upright cylinder around Y, its origin at the center of
its base. Parameters `radius` (0.5 modules), `height` (1 module) and
`segments` (sides around the axis, 16, 3 - 64); connectors base and top; a
convex hull body; mirror plane z; tags `obstacle` and `walkable`. The sides
are one quad per segment, each cap one polygon. The ring's points
(`get_kit_circle_point()`, `kit_shapes.hpp`, shared by the round shapes) are
mirror images of each other to the bit (about z = 0 at every segment count,
about x = 0 at an even one), the points on the axes are exact, and a side edge
uses one (x, z) at both ends.

### Stairs

`basic.stairs` is a straight run of equal steps rising toward -Z from its
front edge at +Z. Parameters `rise` (the height climbed, 1 wall height),
`width` (1 module) and `step_rise_limit` (the highest single step, 0.2 m,
0.05 - 1; never derived): the step count is the least that keeps each step
within the limit and the tread depth is 1.5 times the step rise. Connectors
bottom, top and base as the wedge's; a triangle mesh body; mirror plane x;
tags `walkable` and `stairs`. The side walls are a column per step - a
trapezoid from the base up to the line of the riser feet and the step's
triangle above it - and the base a strip per step, 12 triangles a step (180
at the defaults, 1536 at 128 steps). No vertex has more than six facets: the
riser feet lie on one line, so a side wall without a fan has vertices on the
base, each costing four triangles.

### Floor slab

`basic.floor_slab` is a flat slab with its origin at the center of its base;
its top is the walking surface. Parameters `width` and `depth` (2 modules
each) and `thickness` (0.2 m, 0.01 - 4); slot `floor`; connectors base, top,
and front, back, left and right at the top's edges; a box body;
mirror planes x and z; every side a seam side; tags `walkable`, `floor` and
`tiles`. Six quads over eight corners, 12 triangles.

## Architecture kit

### Wall

`architecture.wall` (`kit_shapes_architecture.cpp`) is a straight wall along
X with its faces toward +Z and -Z, its origin at the center of its base.
Parameters `length` (4 modules), `height` (1 wall height) and `thickness`
(0.25 m); slot `wall`; connectors start and end (the ends, types `wall` and `generic`),
base (type `wall-floor`), top, front and back; a box body; mirror planes x and z; every side a seam side;
tags `obstacle`, `wall` and `tiles`. 12 triangles.

### Door wall

`architecture.door_wall` is a wall along X with a door opening from the
floor at its center in a frame with a lintel; its ends and top meet plain
walls. The wall's parameters (`length` at least 2 m, `height` at least 2.5
m) and `opening_width` (the clear width, 1.2 m, 0.3 - 1.8) and
`opening_height` (2.2 m, 0.5 - 2.4); slots `wall` and `trim`; connectors
start, end, base, top, opening_front and opening_back; a triangle mesh body;
mirror planes x and z; seam sides -x, +x and +y; tags `obstacle`, `wall`,
`opening` and `tiles`. The opening passes the opening check
(`check_kit_opening()`).

- A door frame on both faces: a chamfer from the frame down to the opening's
  edge (the reveal), a flat frame band, and a V groove around the band that
  turns out under the lintel and back over it, so the lintel is a block with
  an ear past each jamb. The frame's face is the wall's face; the opening is
  the stated clear width and height at the reveal's edge, and the ends and
  the top are full rectangles. The frame's widths (chamfer 0.07 m, band
  0.10 m, groove 0.07 m) shrink with a jamb narrower than 0.42 m and its
  heights (lintel 0.20 m) with a header lower than 0.45 m; the groove is at
  most 0.16 and the reveal 0.28 of the thickness deep. The wall has no
  threshold: a sill would lower the stated clear height.
- The frame is the `trim` slot: the reveal, the band out to the center of the
  groove beside it, and the lintel with its ears out to the center of the
  groove around it, on both faces, with the opening's three walls through the
  slab (131 of the 314 facets at every value); the rest of the slab is
  `wall`.
- `Relief_slab` builds it: a grid in the XY plane whose nodes lie at one of a
  few depths below the faces, mirrored about z = 0; a cell between two depths
  is a bevel (a quad), a cell with three corners at one depth is a mitre (two
  triangles split through the fourth corner), and an open cell is an opening
  with a wall through the slab on each of its edges. It holds a slot per cell
  (`set_cell_slot()`), and a wall through the slab takes its solid cell's
  slot.

572 triangles at every value.

### Pillar

`architecture.pillar` is an upright pillar with a base and a capital, its
origin at the center of its base. Parameters `profile` (`square`, `round`),
`width` (the width or diameter, 0.5 modules), `height` (1 wall height) and
`segments` (sides of a round pillar, 12, 3 - 64); slot `trim`; connectors base
and top; a box body for a square pillar, whose plinth and abacus fill its
bounds, a convex hull body for a round one; mirror plane z; a square pillar's seam sides are
-y and +y; tags `obstacle` and `pillar`.

- Round: a base (a plinth and a torus), a shaft that tapers from 0.80 to 0.70
  of the radius, and a capital (an astragal, a necking, an echinus and an
  abacus), all inside the cylinder of the width and the height: the plinth
  and the abacus have the full radius. Base and capital follow the width
  (0.34 and 0.40 of it), take at most 0.20 and 0.22 of a low pillar's height
  and at least 0.02 of a thin tall one's. 15 rings of the segment count, a
  quad per segment between two rings and a polygon per cap: 30 triangles a
  segment less 4 (356 at 12 segments, 1916 at 64).
- Square: a pier of 13 square rings - a plinth, a chamfered base course, a
  shaft inset to 0.80 of the half width, a necking band, a flare and an
  abacus - 100 triangles at every value. The plinth and the abacus have the
  full width, so the pier's bounds are the width's square box. Base and
  capital heights follow the width as the round profile's do.
- `add_lathe()` takes the plan as unit points, so the round and the square
  profile share it.

### Arch

`architecture.arch` is a free-standing masonry arch across X with a
half-elliptic opening of the span and the rise, standing on its two feet,
its faces toward +Z and -Z. Parameters `span` (the clear width at the base,
2 modules), `rise` (the clear height, 0.8 wall heights), `depth` (0.5
modules), `thickness` (of the ring, 0.3 m) and `segments` (the stones, 12,
2 - 48); slots `trim` and `stone`; connectors start and end (beyond the
feet), base and top (at rise + thickness); a triangle mesh body; mirror
planes x and z; tags `obstacle`, `opening` and `arch`.

- The opening is the half ellipse of the span and the rise, its points on
  the ellipse at equal steps of the angle, at least 32 of them and among them
  every point of the `segments`-point half ellipse that
  `get_arch_clear_width()` measures the opening's clear width at a height
  by. The ring is the thickness measured along the ellipse's normal.
- `segments` counts the stones: one keystone at the crown, a springer at each
  foot and voussoirs between them (11 stones at 12 segments: an even count
  has its keystone across the crown and springers of one and a half
  segments; 2 segments make 3 stones). The keystone and the springers fill
  the envelope - the feet span the opening and the thickness on each side,
  the keystone's top is at rise + thickness; the voussoirs lie back from it
  (0.16 of the thickness at the extrados, 0.16 of the half depth on each
  face, 0.05 of the thickness outside the opening's curve), so every joint to
  them is a step. A joint between two voussoirs is a V groove around the
  ring. The curve is smooth shaded inside a stone and the steps and grooves
  are hard edges.
- Slots `trim` (the voussoirs) and `stone` (the keystone and the two
  springers, with the steps down to the voussoirs beside them). An arch of
  two to four segments has no voussoirs: its springers are the `trim` slot
  and its keystone the `stone` slot, so both slots have facets at every
  segment count.

516 triangles at 12 segments, at most 1476 (48 segments).

### Column

`architecture.column` (`kit_shapes_classical.cpp`) is a classical column
standing at its origin, its top the top of a square abacus. Parameters `order`
(`doric`, `ionic`, `corinthian`), `height` (1 wall height), `width` (the
shaft's diameter at its foot, 0.5 modules), `flutes` (0 - 24, 0 a plain
shaft), `taper` (how much narrower the shaft's top is, 0 - 0.3), `seed` (draws
nothing) and `segments` (sides of the base and the capital, 8 - 20); slot
`trim`; connectors base and top (the abacus' top); a convex hull body (it
stands off the shaft by at most the capital's overhang, 0.06 - 0.11 shaft
diameters, under the agent's radius at the usual sizes); no
declared mirror plane (the corinthian leaves have no exact mirror
triangulation).

- The order sets, in shaft diameters: the base (doric: a low torus; ionic and
  corinthian: a square plinth under an Attic base of two tori and a scotia),
  the capital (doric: annulets and a flaring echinus under a 1.26 diameter
  abacus; ionic: a small echinus, two scrolls along Z on the sides with an eye
  standing out of each face and the channel between them, under a thin
  abacus; corinthian: a bell of one diameter with two rows of eight leaves
  curling out and four scrolls reaching for the abacus' corners), and the
  flutes (doric: sharp arrises; ionic and corinthian: flat fillets between
  the grooves).
- The shaft is a lathe of its section through three rings: its foot, a third
  of its height (entasis: the width holds to there) and its top, narrower by
  the taper. Each flute is a rounded groove sampled so the section has about
  twice the segments' points; the section is symmetric about the plane z = 0.
  The base and the capital are round lathes in quads, the plinth and abacus
  boxes; the parts overlap (closed parts, R2.5).
- Base and capital keep at most half the height: a short column squashes them
  vertically, its scrolls with them, so every height keeps a shaft.
- A line layout of columns at the module spacing (2 m) is a colonnade; a
  column stands on a floor slab by base on top and carries an entablature by
  the entablature's base on its top. Replace carries `width` and `height` to
  and from `architecture.pillar`.

At the defaults (doric, 20 flutes) it has 724 triangles (budget 760) and
generates in about 1 ms (Debug); the most is 1560 (corinthian, 24 flutes, 20
segments) in about 3 ms.

### Entablature

`architecture.entablature` (`kit_shapes_classical.cpp`) is a classical beam run
along X that rests on column tops. Parameters `length` (2 modules), `height`
(0.25 wall heights), `depth` (the architrave's, 0.5 modules), `cornice` (the
corona's projection beyond the architrave, 0 - 0.3 m), `frieze` (`triglyph`,
`plain`) and `seed` (draws nothing); slot `trim`; connectors base, top, start
and end (the base's end centers); a triangle mesh body (a hull would fill the
space under the cornice and before the recessed frieze); mirror planes x and z.
It is a rider (`doc/editor/level_tools.md` "Riders"): its base accepts `rail`
and `generic`, and it maps `child.length` to `length`.

- The profile, bottom to top: two fasciae and a taenia (the architrave, 0.35
  of the height), the frieze recessed 4 cm behind the architrave's face (0.38),
  a bed moulding, the corona and a cyma (the cornice). It is extruded along X;
  each band's end is one quad, or where a narrower neighbouring band's
  corners lie on its line, a middle quad and two side pieces, so the ends are
  their own mirror images.
- Triglyphs stand 5.5 cm out of the frieze on both faces: at every bay's ends
  and center, the bays about 1.6 frieze heights long (at most 16 per run),
  each three bars with the two glyphs between them and a regula under the
  taenia. The run's ends carry half triglyphs, so runs joined end to start
  make whole ones; both ends' outlines are equal, so runs join flush.

At the defaults it has 540 triangles (budget 560), about 0.5 ms; the most is
1692 (8 m long, 0.2 m high: 16 bays of triglyphs).

### Torii

`architecture.torii` (`kit_shapes_japanese.cpp`) is a Japanese gate across X
standing at its origin, walked through along Z. Parameters `width` (the posts'
feet apart, 3 m), `height` (the top lintel's top at the center, 3.6 m; both in
meters, because the agent fixes the least opening at every level length), `curve` (how far the top lintel's ends turn up, a share of the width,
0 - 0.2; in the default mask), `post_width` (0.3 m), `lean` (the posts' inward
lean, 0 - 8 degrees), `seed` and `segments` (stations along the lintels, 2 -
24); slot `wood`; connectors base, opening_front and opening_back (the
threshold on each face); a triangle mesh body; mirror planes x and z; tags
`opening` and `japanese`.

- Two round posts of horizontal caps (a leaning post's foot lies flat) on
  stone sleeves, a straight tie beam (`Kit_mesh_builder::add_beam()`) through
  the posts and out past them, a strut from it to the lower lintel, and the
  lower and the top lintel, both following the top lintel's curve: its
  underside rises with the square of the distance from the center, the curve
  times the width at its ends. The lintels are closed strips of quads between
  vertical stations.
- Refused combinations (`check_combination`): posts whose lean brings their
  tops closer than a post's width. Walkability (R2.10): the clear width
  between the posts at the agent's height and the tie beam's underside pass
  the opening check; the derive ranges keep a derived torii accepted and
  walkable at any level lengths.
- A path or line layout of torii makes a gate tunnel; the composition image
  stands four in vermilion (`Kit Vermilion`) along a path slab.

At the defaults it has 400 triangles (budget 420), about 0.4 ms; the most is
592 (1 m wide, 24 stations).

### Battlement

`architecture.battlement` (`kit_shapes_medieval.cpp`) is a medieval parapet
run along X on a wall top. Parameters `length` (2 modules), `height` (the
crenels' sill, 0.9 m), `thickness` (the wall's default, 0.25 m),
`merlon_height` (above the sill, 0.9 m), `merlon_width` (1.2 m) and
`crenel_width` (0.8 m) (nominal: the bays fit the length), `merlon`
(`square`, `swallowtail`, `pointed`) and `seed`; slots `wall` (the masonry)
and `trim` (the dressed stone); connectors base, start and end; a triangle
mesh body; mirror planes x and z; no seam side, since the band and the
copings stand out of the faces (bounds along Z: the thickness and twice the
overhang).

- A closed part per piece of masonry, overlapping. A breastwork (a box flush
  with the wall it stands on) carries a sill band - the crenels' sill stones
  as one course of the `trim` slot, at most 0.07 m high, standing at most
  0.035 m out of both faces with weathered edges - and on the band one merlon
  per bay: a block with chamfered upright edges (at most 0.04 m), pierced by
  an arrow loop (0.1 m wide, 0.55 of the merlon's height under its lowest top
  line) whose reveal is splayed on both faces, under a coping of the `trim`
  slot that overhangs the faces - a saddleback over a `square` merlon, a slab
  along the two top lines of a `swallowtail` or a `pointed` one. The stated
  top (`height` + `merlon_height`) is the copings' highest line. The detail's
  sizes follow the merlon's width, its height and the thickness where those
  leave little room.
- A bay is a merlon between two half crenels, so runs join into whole
  crenels, every merlon and coping is whole, and a run's two ends are the
  breastwork and the band alone, equal at every value; at most 22 bays.
- It rests on a wall's top by base on top, keeping the wall's direction; a
  line layout of battlements without a step steps by their length, end to
  end along a wall. Replace carries `length`, `thickness` and `height`
  (clamped to the sill's range) to and from `architecture.wall`.
- It is a rider (`doc/editor/level_tools.md` "Riders"): its base accepts
  `rail` and `generic`, and it maps `child.length` to `length` and
  `wall.thickness` to `thickness`, so on a wall's top it takes the wall's
  length and thickness; its bay count follows the length.

112 triangles at the defaults, 80 a bay with square merlons and 88 with the
two other forms plus 32, at most 1968 (22 bays).

### Tower

`architecture.tower` (`kit_shapes_medieval.cpp`) is a medieval tower segment
standing at its origin. Parameters `profile` (`round`, `square`), `width` (the
outer width above the batter, 3 m: in meters, because the doorway and the
agent fix the least width at every module), `height` (the top floor, 1.5 wall
heights), `batter` (the base's outward slope, 0 - 10 degrees), `wall`
(0.4 m), `opening_width` and `opening_height` (the doorway's clear size,
fixed: 0.8 - 1.8 m and 2 - 3.4 m, default 1.2 by 2.2 m, the agent's clearance
at their minimums), `top` (`battlement`, `flat`), `slit_rows` (rows of arrow
slits, 0 - 2, default 1) and `seed`; slots `wall` (the masonry), `trim` (the
dressed stone) and `floor` (the walkable top at `height`); connectors base,
top (the top floor's center), opening_front (the surround's front at the
ground), left, right and back (the sides' wall middles at the base, where
joined walls end inside the tower's wall); a triangle mesh body; tags
`walkable`, `opening` and `medieval`; origin rule `base_contains_origin`. The
tower declares no mirror plane. Closed, overlapping parts:

- the wall, an upright shell between an outer and an inner ring from the
  ground to inside the top floor, cut through by the doorway (0.04 m wider on
  each side than the clear opening and 0.1 m higher) and by the slits;
- the base, a plinth course 0.35 m high and 0.07 m out with a chamfer and
  above it the batter up to the cut's head, around the tower from one jamb
  stone to the other; its section has five points at every batter, the
  batter's face ending at least 5 mm inside the wall's face (at a batter of 0
  an upright face just inside it);
- a string course over the batter, around the whole tower;
- the doorway's surround - two jamb stones 0.28 m wide through the base, whose
  faces are the clear width, and a gabled lintel on them, whose underside is
  the clear height - standing out of the plinth;
- arrow slits 0.9 m high in rows 1.5 m apart from 0.4 m above the cut's head
  (as many of `slit_rows` as stay clear of the corbels, or of a flat top's
  floor): 0.28 m wide on the outer face, 0.09 m at the throat 0.4 of the wall
  in, and up to 0.44 m on the inner face - on a round tower four, a third of a
  half turn before and after each side connector (the front two only where
  the doorway's jambs leave them room), on a square tower two on each of the
  side and back faces;
- with the `battlement` top a top floor that stands 0.25 m out on corbels (one
  at each corner, and one to seven along each side of a square tower), a
  parapet 0.35 m thick with a breastwork 1 m above the walkway, and merlons to
  0.9 m above it under saddleback copings - a round tower has three sides per
  merlon, five or six merlons (15 or 18 sides), a crenel over the doorway; a
  square tower's sides have corner merlons and one to six crenels between
  merlons 1.6 times as wide; with the `flat` top the floor as a course 0.04 m
  out of the wall, which a further segment stands on by base on top.

The walkway is 0.1 m narrower than the wall's outer face all around. The
doorway's clear box holds no vertex at any value. The wall's rings hold the
plan's corners but those within 0.07 m of a jamb or inside a slit, and the
jambs; a facet through the wall joins the two rings at each jamb and at each
corner both rings have on the same side of the doorway, and the level faces
between such lines are quads or `add_band()` strips.

- Refused combinations: an inside narrower than the doorway and 0.4 m, and a
  height less than the doorway's and 0.9 m. Walkability (R2.10): the doorway
  passes the opening check, the inside and the walkway are at least the
  agent's width; the derive ranges keep a derived tower accepted and walkable
  at any level lengths.
- A wall's start joined to the tower's right side ends inside the tower's
  wall; a navigation build reaches the hollow through the doorway, and the
  top is a walkable floor (an island of its own until stairs reach it).

1524 triangles at the defaults (round, battlement top, one row of four
slits), at most 1940 (square, 16 m wide, two rows); more than 10 parts.

### Roof

`architecture.roof` (`kit_shapes_roof.cpp`) is a roof over a rectangle of
wall lines (the walls' center lines), its base at the walls' top. Parameters
`form` (`gable`, `hip`, `shed`, `pyramid`), `width` and `depth` (4 modules
each), `pitch` (15 - 60 degrees, 35), `overhang` (the eaves beyond the wall
lines, 0 - 0.6 m, 0.3), `curve` (0 - 0.3: the slope's height along its run is
`s - 2.5 curve s (1 - s)`, so the eaves flatten and the ridge steepens: the
Japanese roof) and `seed` (draws nothing); slots `roof` (the slab, tiles and
caps; Kit Roof Tile) and `trim` (the body: gable ends and underside);
connectors base, left, right, front and back (on the base plane at the wall
lines, facing down, their up axes facing out of the room); a triangle mesh
body; mirror plane x (a shed rises toward -Z); origin rule
`base_contains_origin`.

- The slab is 0.15 m thick (vertically), its eaves' underside at y = 0. The
  gable and the shed are a section along Z extruded along X over the width
  plus the overhang at each rake end; the hip and the pyramid are rings of
  four corners from the eave to the ridge (a hip roof whose ridge would be
  shorter than 0.1 m, and a pyramid, end in an apex), a ring collapsing to
  the ridge or the apex giving triangles at the hip ends.
- The body closes the roof below: the stations inside the wall lines
  (2 cm in, so it meets no slab vertex at any overhang) from y = 0 up into the
  slab's middle; its gable ends are the gable's trim triangles.
- Tile courses about 0.32 m along the slope (2 - 14 per slope): each a
  closed strip on the slab, its butt 5 cm above it at the lower edge, 1.2 cm
  at the upper, ending 2 cm short of the next station and 1.5 cm short of a
  hip line; a ridge cap (gable, hip), hip caps (in pieces of two stations
  over a curved roof) and a finial on a pyramid.
- On walls: four walls whose fronts face out, the roof's front connector
  joined to the front wall's top, the roof covers the room centred on it;
  a door wall's top serves the same.

At the defaults (gable, 4 by 4 m) it has 508 triangles (budget 520), about
0.3 ms; the most is 1360 (a curved hip roof).

### Window wall

`architecture.window_wall` (`kit_shapes_build_13_16.cpp`) is
`architecture.wall` with a window opening at its center. Parameters `length`
(4 modules, at least 2 m), `height` (1 wall height, at least 2.5 m),
`thickness` (0.25 m), `opening_width` (1 m, 0.3 - 4), `opening_height` (the
clear height from the sill to the crown of the head, 1.2 m, 0.3 - 6),
`sill_height` (0.9 m, 0.1 - 4), `head` (`flat`, `round`, `pointed`),
`mullions` (upright bars, 1, 0 - 4), `transoms` (level bars, 1, 0 - 3), `seed`
(draws nothing) and `segments` (along a curved head, 12, 4 - 32; an odd count
uses one less); slots `wall`, `trim` (the surround) and `wood` (the casing
and its bars); the wall's six connectors (start, end, base, top, front, back);
a triangle mesh body; mirror planes x and z; seam sides -x, +x, -y and +y, so
it joins plain walls end to end and carries what a wall carries.

- Refused combinations (`check_combination`): an opening that leaves less
  than 0.15 m of wall beside it (`length` against `opening_width`) or above
  it (`height` against `sill_height` plus `opening_height`). The least length
  and height hold every derived opening.
- The opening's outline is the sill's two ends and the head: two corners
  (`flat`), half an ellipse of the half width's rise, at most half the
  opening height (`round`), or two arcs with their centers on the springing
  line that rise 1.5 half widths, at most 0.6 of the opening height
  (`pointed`). The head's points are `make_kit_arch_head_points()`
  ("Shared construction").
- Both faces carry the same surround, cut into the wall so nothing stands
  out of its thickness: five rings, each the outline moved out by a distance
  (mitred at its corners, the sill by its own larger distance): the reveal's
  edge 0.054 m inside the face, a splay out to the face (0.06 m, the sill's
  0.13 m: the sill slopes), a flat band (0.10 m), and a groove 0.03 m deep
  and 0.06 m wide whose center is the border between the `trim` and the
  `wall` slot. A quad between two rings is planar, since moved edges stay
  parallel. The surround keeps these sizes where the jambs and the header are
  0.32 m or more and the sill 0.38 m or more above the base, and shrinks in
  proportion below that; its depths are at most 0.28 (reveal) and 0.16
  (groove) of the thickness.
- A wall face outside the surround is a quad under the sill and fans of
  triangles about the two top corners over the outermost ring, mirrored
  about x = 0; the ends, the top and the bottom are one quad each.
- The casing is a ring of wood 0.06 m deep in the middle of the wall: 0.05 m
  into the opening (less in an opening too small for it), its outer edge
  0.01 m inside the wall. Mullions divide the width evenly and run from the
  sill into the wall above the head; transoms divide the height from the sill
  to the head (`flat`) or to the springing line, where the highest one lies
  (`round`, `pointed`). Casing and bars are closed parts of their own.
- Walkability (R2.10): a sill higher than the agent's climb makes no way
  through, which every derived sill is; with a sill the agent climbs the
  opening is a doorway and has to pass the door wall's opening check.
- Replace carries `length`, `height` and `thickness` to and from
  `architecture.wall`, and the opening's width and height to and from
  `architecture.door_wall`.

At the defaults (a flat head, one mullion, one transom) it has 152 triangles
(budget 160); a curved head of 12 segments has 460, the most is 1080 (32
segments, 4 mullions, 3 transoms).

### Pediment

`architecture.pediment` is a classical gable front along X that stands on
an entablature. Parameters `width` (4 modules), `pitch` (the rakes' slope,
18 degrees, 8 - 45), `depth` (0.5 modules), `recess` (how far the field lies
behind the cornice's bed, 0.1 m, 0.01 - 0.5; at most half of what the depth
leaves), `dentils` (under each rake, 9, 0 - 16) and `seed` (draws nothing);
slots `trim` and `wall` (the field); connectors base, start and end (the
ends of the base); a triangle mesh body; mirror planes x and z; seam side -y.

- The outline is the bottom, two upright eave ends and the two rakes to the
  apex over the origin; the apex stands the eave's height plus the half
  width's rise above the base. Both faces are alike.
- A face steps back in three nested triangles, each inside the rakes by its
  inset and above the bottom by its base: the sima at the face, the corona
  one step behind it, the bed three steps behind, and the field behind the
  bed by the recess. The mouldings' measure is 0.08 of the half width, at
  most 0.26 of the half width's rise and 1.2 depths, so a narrow, a low and a thin
  pediment keep a field; a step is 0.3 of it, at most 0.07 of the depth.
- Dentils stand plumb on the bed under each rake on both faces, evenly from
  the apex to the field's corner, each half its spacing wide, its head inside
  the corona; each is a closed part of its own.
- It rests on an entablature's top by base on top; a line of columns under an
  entablature under a pediment is a temple front.

At the defaults it has 520 triangles (budget 540): 88 and 48 for each of the
count of dentils; the most is 856.

### Stepped platform

`architecture.stepped_platform` is tiers of slabs, each inset from the one
below: a platform with one tier, a stylobate with three, a stepped pyramid
with nine. Parameters `width`, `depth` (4 modules each), `tiers` (3, 1 - 9),
`tier_rise` (0.2 m, 0.05 - 4), `tier_inset` (each tier's step in, 0.3 m,
0.05 - 8) and `seed` (draws nothing); slots `stone` and `floor` (the deck);
connectors base, top (the deck's center), front, back, left and right (the
centers of the base's edges); a box body for one tier, a triangle mesh body
for more; mirror planes x and z; seam side -y; tag `walkable`.

- One closed mesh: per tier a riser, a chamfer round its top edge (0.12 of the
  rise, at most 0.06 m and a quarter of the inset) and the tread to the
  next tier's foot. The top is a rim of stone (0.18 m, at most a quarter of
  the top's smaller half side) around the deck.
- The inset shrinks where the tiers would leave less than 0.4 of the smaller
  side for the top (`tier_inset` at most 0.3 of that side over the tiers less
  one), so every value set is a platform and none is refused.
- With a tier rise within the agent's climb (0.4 m) and an inset of at least
  the rise every side is stairs; it states no walkability check, since a
  pyramid of high tiers is an obstacle by intent.
- It carries columns on its deck by base on top; a grid layout of platforms
  tiles by their base's edge connectors.

At the defaults it has 76 triangles (budget 80): 24 for each tier and 4; the
most is 220.

### Timber bay

`architecture.timber_bay` is one bay of a Japanese post-and-beam wall along X.
Parameters `length` (2 modules, 0.5 - 16 m), `height` (1 wall height, 1 - 8 m),
`post_width` (width and depth of a whole post, 0.15 m, 0.06 - 0.4), `infill`
(`open`, `shoji`, `plaster`, `half`; default `shoji`), `rails` (0 - 3, default 1),
`panels` (1 - 4, default 2) and `seed`; slots `wood` and `wall`; the wall's six
connectors (start, end, base, top, front, back; front and back on the posts'
faces); a triangle mesh body; mirror planes x and z; tags `wall`, `japanese`
and `tiles`.

- The frame is seven boxes: a half post at each end (two bays joined end to
  start make a whole post, as the battlement's half merlons), a top beam of the
  whole length, a sill, a lintel with its underside at 0.62 of the height, a
  tie rail of the whole length that stands out of the posts on both faces just
  above the lintel, and a plaster field (slot `wall`) between the lintel and
  the beam. Every member's size is a share of the post width, bounded by a
  share of the height or the length, so a short or low bay keeps its parts.
- `open`: nothing below the lintel; the sill is a low threshold.
- `shoji`: `panels` sliding screens side by side over a low threshold. A screen
  is two stiles, a top and a bottom rail, a hip board under a mid rail, and a
  lattice over paper (slot `wall`): 5, 3 or 2 upright bars for 1, 2 or more
  screens, and 2 + 2 * `rails` level bars.
- `plaster`: a plaster field from the sill to the lintel, `rails` rails across
  it and a stud between two of its `panels` fields.
- `half`: the plaster field with its rails and studs reaches 0.45 of the way
  to the lintel under a cap rail; above it the bay is open.
- The shape has no walkability check and no `opening` tag: an open bay is a
  frame, not a stated doorway.

Derive ranges: `length` 1.5 - 4 modules (step 0.5), `height` 0.85 - 1.25 wall
heights (0.05), `post_width` 0.12 - 0.24 m (0.03), `infill` all values, `rails`
0 - 2, `panels` 1 - 3.

At the defaults it has 420 triangles (budget 440); the most is 900 (four
screens with three rails).

### Railing

`architecture.railing` is a railing or fence run along X in equal bays.
Parameters `length` (2 modules, 0.5 - 16 m), `height` (the posts' height, 1 m,
0.4 - 2), `bay_length` (the nominal bay, 1 module, 0.25 - 8), `post_width`
(0.12 m, 0.04 - 0.3; at most 0.35 of a bay), `style` (`rail`, `picket`,
`balustrade`, `lattice`; default `balustrade`), `infill` (1 - 8, default 5) and
`seed`; slot `trim`; connectors start, end and base; a triangle mesh body;
mirror planes x and z; tags `railing`, `fence` and `tiles`.

- The bay count is the count (1 - 32) whose bays, `length` / count, are nearest
  to `bay_length`, the smaller count on a tie (`get_kit_repeat_count()`), so a
  run that takes another length keeps its bays about `bay_length` long. The
  posts stand `length` / count apart.
- A run of more than six bays may take as many more triangles than its kit's
  ceiling as it has more bays (`Kit_shape_definition::get_ceiling_scale`,
  `Kit_shape_library::get_triangle_ceiling()`).
- It is a rider (`doc/editor/level_tools.md` "Riders"): its base accepts
  `rail` and `generic`, and it maps `child.length` to `length`, so on a
  floor's rail or a wall's top it takes the host's length.
- A half post stands at each end (two runs joined make a whole post) and a
  post between two bays, each a shaft under a pyramid whose apex stands over
  the post's center line (an end post's over the run's end). The balustrade's
  and the lattice's posts carry a collar slab under the pyramid.
- `rail`: level rails through the posts, one for every two of `infill` (1 - 4).
- `picket`: two stringers through the posts and `infill` pointed pickets in
  each bay, thicker than the stringers.
- `balustrade`: a plinth and a handrail through the posts and `infill` turned
  balusters in each bay. The more balusters the run has, the fewer triangles
  each takes: up to 6 in all 8 sides and 8 stations (124 triangles), up to 12
  6 sides and 6 stations (68), up to 24 4 sides and 6 stations (44), more 4
  sides and 4 stations (28).
- `lattice`: a low and a top rail through the posts and `infill` crosses of
  two slats in each bay.

Derive ranges: `length` 1.5 - 4 modules (0.5), `height` 0.9 - 1.2 m (0.05),
`bay_length` 0.75 - 2 modules (0.25), `post_width` 0.1 - 0.16 m (0.02), `style`
all values, `infill` 3 - 6.

At the defaults it has 782 triangles (budget 800); the most is 1550 (a
balustrade of 6 bays of 8).

### Stairs with side walls

`architecture.stairs` is stone stairs between side walls. Parameters `form`
(`walled`, `l_landing`, `u_landing`), `rise` (1 wall height, 0.2 - 12 m),
`width` (the clear width between the walls, 1.2 m, 0.3 - 8; in meters, because
the agent fixes the least width at every level length), `step_rise_limit`
(0.2 m, 0.05 - 1; never derived), `wall_height` (of the walls above the steps'
edges, 0.9 m, 0.05 - 1.5) and `seed`; slots `stone` and `trim`; connectors
bottom, top and base; a triangle mesh body; the origin rule is
`base_contains_origin`; tags `walkable` and `stairs`.

- Steps follow `basic.stairs`: the step count is the least that keeps a step
  within the limit, the tread depth 1.5 times the step rise. At most 64 steps;
  more are refused, and so is a landing form whose rise makes one step.
- A flight is a stepped stone block from the ground under a tread slab per
  step (slot `trim`) with a nosing over the riser below. `walled`: one flight
  rising toward -Z, its foot at +Z, the origin at the center of the run as
  `basic.stairs`. `l_landing`: half the steps (rounded down) rise toward -Z to
  a square landing of the width, the rest rise toward +X from its right edge.
  `u_landing`: the landing spans both flights and the second flight comes back
  toward +Z beside the first, 1.6 wall thicknesses apart. The origin of a
  landing form is the center of the bounds of the steps and the landing.
- The side walls stand from the ground (the thickness is 0.15 of the width,
  0.1 - 0.35 m), their tops parallel to the steps' edges and level over the
  top tread and around the landing's outer sides, under a coping (slot
  `trim`), with a string course on both faces at the height of the steps'
  edges. A pier with a cap stands at every wall end: at the foot, at the
  landing's corners and at the top. A `wall_height` at its least makes a curb.
- Connectors: `bottom` on the ground before the first riser facing +Z; `top`
  at the far edge of the last tread at the rise, facing the way the last
  flight climbs (-Z, +X or +Z); `base` at the origin.
- Walkability (R2.10): the step rise, the slope and the width against the
  agent, as `basic.stairs`; the derive ranges hold at any level lengths.

Derive ranges: `form` all values, `rise` 0.25 - 1.5 wall heights (0.05),
`width` 1 - 2 m (0.1), `wall_height` 0.3 - 1.1 m (0.1).

At the defaults (15 steps) it has 520 triangles (budget 540); the most is
1856 (a U landing with 64 steps).

### Spiral stairs

`architecture.spiral_stairs` is medieval spiral stairs about a newel at the
origin. Parameters `rise` (1 wall height, 0.2 - 12 m), `radius` (of the steps'
outer edge, 1.25 m, 0.6 - 4; in meters, because the agent fixes the least),
`step_rise_limit` (0.2 m, 0.05 - 0.4; never derived), `turn` (`left`,
`right`), `newel_width` (0.3 m, 0.1 - 1) and `seed`; slots `stone` and `trim`
(the treads); connectors bottom, top and base; a triangle mesh body; the
origin rule is `base_contains_origin`; tags `walkable`, `stairs` and
`medieval`.

- The step count is that of `basic.stairs`. The stairs climb 2.4 m in a turn
  at every step rise, so a step turns 2 pi times its rise over 2.4 m (30
  degrees at a 0.2 m step) and the headroom under the turn above is 2.4 m less
  a step's thickness. At most 96 steps; a newel wider than the radius is
  refused.
- A step is a wedge of five corners (its outer edge two chords) 1.35 step
  rises thick, from inside the newel to the radius; it reaches 0.12 of its
  angle before its riser (the nosing) and 0.1 under the next step, so each
  step laps the one below. The first step stands on the ground.
- The newel is a prism of ten sides to the rise, on a plinth and under a
  collar that ends at the rise, so stairs stack.
- The first riser lies along +X (a right turn: -X) and the steps rise toward
  -Z; a right turn is the left turn's mirror image across x = 0. `bottom` lies
  on the ground at the middle of the first riser facing +Z, `top` at the
  middle of the last step's far edge at the rise, facing the way the steps
  turn.
- Walkability (R2.10) on the line the agent's center walks closest to the
  newel (the newel's radius plus the agent's): the step rise, the slope there,
  the clear width from the newel to the radius, and the headroom.

Derive ranges: `rise` 0.5 - 1.5 wall heights (0.05), `radius` 1.15 - 1.6 m
(0.05), `turn` both values, `newel_width` 0.25 - 0.4 m (0.05).

At the defaults (15 steps, 1.25 turns) it has 348 triangles (budget 360); the
most is 1644 (96 steps).

### Bridge deck

`architecture.bridge_deck` is a walkable deck along X between its two ends.
Parameters `length` (4 modules, 1 - 32 m), `width` (2 m, 1.2 - 8; in meters and
not derivable, since the agent fixes the least width), `thickness` (the slab's,
0.3 m, 0.1 - 1; at most 0.4 of the height), `height` (the walking surface over
the base on piers or arches, 0.75 wall heights, 0.5 - 16 m), `supports` (`none`,
`piers`, `arched`; default `arched`), `spans` (bays between supports, 1 - 4,
default 1), `parapet` (`none`, `solid`, `open`; default `solid`),
`parapet_height` (0.7 m, 0.2 - 1.5), `courses` (courses of paving stones along
the deck, 1 - 16, default 8) and `seed`; slots `stone`, `floor` and `trim`;
connectors start and end (at the ends of the walking surface, facing -X and +X)
and base; a triangle mesh body; mirror planes x and z; tags `walkable`,
`bridge`, `floor` and `tiles`.

- The deck is a slab (slot `trim`, a string course along both faces) under
  paving stones (slot `floor`) whose tops are the walking surface. The courses
  run across the deck, two and three stones to a course in turn from both ends,
  so the joints break and the deck mirrors. A kerb of stone runs along each
  edge, level with the paving.
- `parapet` `none` leaves the kerbs. `solid` raises them to walls under a
  coping (slot `trim`). `open` leaves a low curb under two rails and three
  balusters to a bay (two with three or four spans), all slot `trim`. A solid
  or open parapet has a post under a cap over every support, half a post at the
  deck's ends.
- `supports` `none`: the deck lies on two beams along it and a cross beam at
  every support's place (slot `trim`); its walking surface is the beams' height
  (0.8 of the thickness, 0.1 - 0.5 m) plus the thickness over the base, and
  `height` has no effect.
- `piers`: a pier on a plinth under a cap (slot `trim`) at every support, two
  beams along the deck on the caps.
- `arched`: a pier on a plinth at every support with an impost band at the
  springing line, and in every bay an arch of voussoirs through the deck's
  width (slots `trim` and `stone` in turn, an odd count so they mirror about the
  crown: 15, 11, 9 or 7 for 1 to 4 spans), a spandrel wall behind their faces up
  to the slab, and a keystone (slot `trim`). The arch is half an ellipse: its
  rise is the opening's half width where the height allows, less under a low
  deck.
- Half piers and half posts stand at the deck's ends, so two decks joined end
  to start make whole ones (as the railing's half posts).
- Walkability (R2.10): the clear width is the whole width without a parapet,
  and the width between the posts with one (the width less two wall
  thicknesses and four lips: 0.86 m at the least width, 1.46 m at the default);
  the check refuses a deck whose clear width is under the agent's width plus
  the opening margin. The paving stones' tops are level at one height at every
  value.

Derive ranges: `length` 3 - 8 modules (step 1), `thickness` 0.2 - 0.4 m (0.05),
`height` 0.5 - 1.25 wall heights (0.05), `supports` all values, `spans` 1 - 2,
`parapet` all values, `parapet_height` 0.5 - 1 m (0.1), `courses` 6 - 10. No
parameter is in the default mask.

At the defaults it has 728 triangles (budget 760); the most is 1848 (four
arched spans, railings, sixteen courses).

### Buttress

`architecture.buttress` is a medieval stepped buttress against a wall face at
its back (z = -depth / 2). Parameters `height` (1 wall height, 1 - 16 m),
`depth` (how far the foot stands out of the wall, 1 module, 0.3 - 6 m), `width`
(0.5 modules, 0.2 - 3 m), `steps` (stages, 1 - 4, default 3), `cap`
(`weathered`, `gabled`, `pinnacle`; default `gabled`) and `seed`; slots `stone`
and `trim`; connectors back (on the wall face at the base, facing -Z) and base;
a convex hull body; mirror plane x; tags `buttress` and `medieval`.

- A plinth with a chamfered front edge fills the width and the depth. On it
  stand `steps` stages, each a profile extruded across the width: every stage
  stands 0.55 / `steps` of the depth less far out than the one below and is a
  little narrower, a lower stage is higher than an upper one, and a sloping
  offset leads from a stage's front edge back to the next stage's face over a
  drip course (slot `trim`).
- `weathered`: the last stage ends under a slope from its front edge back to
  the wall at the full height. `gabled`: it ends level under a gablet on a base
  course, the ridge running out from the wall. `pinnacle`: it ends level under
  a square pinnacle at its front: a base course, a shaft under a band and a
  spire to the full height.
- The bounds are the width by the height by the depth at every value.

Derive ranges: `height` 0.8 - 1.2 wall heights (step 0.05), `depth` 0.75 - 1.5
modules (0.25), `width` 0.4 - 0.7 modules (0.05), `steps` 1 - 4, `cap` all
values. No parameter is in the default mask.

At the defaults it has 104 triangles (budget 110); the most is 154 (four
stages under a pinnacle).

### Stone lantern

`architecture.stone_lantern` (`kit_shapes_build_21_24.cpp`) is a Japanese
stone lantern at its origin that carries a light. From the ground up:
a pedestal of a ground slab, a step and a shoulder that falls away from the
shaft's foot; a shaft with rings at its foot, middle and head; a platform that
flares to a slab; the light box; a broad cap; and a jewel on a collar. Heights
are shares of `height`, plan sizes shares of `width`, so the two scale the
lantern independently.

- Parameters: `height` (meters, default 1.5, 0.6 - 4), `width` (meters,
  default 0.6, 0.3 - 2: the cap's size along X), `cap` (`square`,
  `hexagonal`; default `hexagonal`), `cap_curve` (ratio, default 0.5, 0 - 1;
  in the default derive mask) and `seed`. The generator draws no random
  numbers; the seed only derives values.
- `cap` is the plan of the cap, the light box, the platform and the pedestal.
  A hexagonal lantern has two corners on the X axis (its size along Z is 0.866
  of the width) and a round shaft of twelve sides; a square lantern has its
  sides on the axes and a square shaft.
- The light box is a sill and a head, a post on every corner and a window
  panel on every side around a flame in the slot `glow`: a small solid of
  three rings on the sill (0.604 - 0.646 of the height), so the light above
  it is enclosed by no geometry and with shadow casting on it still leaves
  through the windows. The sides toward +Z
  and -Z have open windows (a rectangular opening in a frame), the other
  sides round moon windows of twelve points.
- The cap's slope is steep under the jewel and level at the eaves, with a
  ridge down to every corner. `cap_curve` turns the corners up (by up to 0.36
  of the half width, at most 0.08 of the height) and bows the sides inward (by
  up to 0.07 of their distance from the axis); at 0 the eaves are level and
  straight.
- Slots: `stone` and `glow`.
- Connector: `base` at the origin, facing -Y.
- Light frame: `light`, a point light on the axis at the middle of the light
  box (0.6725 of the height), direction -Y, starting at 12 cd with a range of
  6 m and the color (1, 0.72, 0.42): a flame behind stone, dimmer than the
  kit's starting 40 cd ("Shared construction").
  Shadow casting starts off (`doc/editor/level_tools.md` "Lights on pieces"); turned on, the light still
  lights the box through its windows, and the platform under the box shades
  the ground near the lantern.
- Body: convex hull. Mirror planes: x and z (every ring's sides are built
  from integer weights and the band diagonals leave each side's middle, so
  the mirror images are equal to the bit).
- Mesh: 27 closed parts under a hexagonal cap (1864 triangles), 21 under a
  square one (1296); the topology follows from `cap` alone.

### Tunnel

`architecture.tunnel` is a tunnel segment along X. Parameters `length`
(2 modules, 1 - 32 m), `bore_width` (2 m, 1.2 - 8) and `bore_height` (2.4 m,
2.3 - 8; both in meters and not derivable, since the agent fixes the least
bore), `wall` (the stone beside and over the bore, 0.3 m, 0.1 - 2), `profile`
(`round`, `pointed`, `square`; default `round`), `seed` and `segments` (along a
round or a pointed head, 2 - 48, default 12); slots `stone` and `floor`;
connectors start and end (on the floor at the two mouths, facing -X and +X)
and base; a triangle mesh body; mirror plane x; tags `walkable`, `opening`,
`tunnel` and `tiles`.

- One part: a block the bore's width and two walls wide and the floor, the
  bore's height and a wall high, with the bore through it along X.
- The bore stands on a floor 0.1 m over the base (slot `floor`), between
  upright jambs under its head: `round` half an ellipse and `pointed` two arcs
  (`make_kit_arch_head_points()`), whose rise is at most 0.6 of the bore's
  height; `square` a lintel.
- Segments join end to start along a line and a path.
- Walkability (R2.10): the check holds the bore's height and its clear width
  at the agent's height plus the opening margin over the floor to the agent
  (`check_kit_opening()`).
- 4 * (head points + 2) + 32 triangles.

Derive ranges: `length` 2 - 6 modules (step 1), `wall` 0.2 - 0.5 m (0.05),
`profile` all values. No parameter is in the default mask.

At the defaults it has 92 triangles (budget 100); the most is 236 (a round head
of 48 segments).

## Nature kit

### Rock

`nature.rock` (`kit_shapes_nature.cpp`) is one to four pieces, the largest on
the origin and the smaller ones (0.62, 0.45 and 0.30 of its size) against its
flank. Parameters `form` (`boulder`, `angular`: tall shards, `slab`: low,
wide, one ledge, `layered`: ledges and overhangs, a third of the sides
sheer), `size` (the largest piece's width, 1 module), `roughness` (irregular
sides, dip, lean, tilt), `pieces` (1 - 4, default 3) and `seed`; slot `rock`;
a convex hull body; tags `obstacle` and `decoration`.

- A piece is a stack of convex rings on a flat base. Every ring has the same
  edge directions, so each side face between two rings is a planar trapezoid
  and the ring lines read as strata; a side whose offsets run straight from
  foot to top is one fracture plane; the other sides stand steeper or slope in
  earlier by a draw. The strata dip and the piece leans by one affine map, the
  base ring is the lowest level's side planes cut by the ground, the top face
  tilts by itself (the band under it is triangles) and the boulder and the
  angular form end in an apex inside the top ring. A ring's offsets are moved
  toward the footprint's by the least share that keeps every edge at least 45
  percent of its length, which keeps each ring a valid polygon without a
  decision.

162 triangles at the defaults.

### Land mass

`nature.land_mass` (`kit_shapes_nature.cpp`) is a plateau with a lobed
outline (the radius between 68 and 100 percent of half the footprint) and a
flat walkable top. Parameters `footprint` (the foot's diameter before the
lobes, 4 modules), `height` (of the flat top, 1 module), `erosion` (depth of
the gullies and roughness of the cliff, 0 - 1), `seed` and `segments`
(columns around the axis, 8 - 48, default 20); slots `grass` (top, rim,
benches), `earth` (soil band, talus, base) and `rock` (cliff); connectors
base and top; a triangle mesh body; tags `walkable` and `ground`.

- Seven rings of `segments` columns from the ground up: a talus foot, a cliff
  with gullies down the columns, a bench of turf over two windows of the
  perimeter, a soil band and a turf rim that overhangs the cliff. The benches
  are found by column index, so the slot of a facet is no float decision.
- The walkability check holds the top at least the agent's width across at
  the outline's deepest lobe.

280 triangles at the defaults.

### Hill

`nature.hill` is a hill with a lobed foot and its crest off center, with
walkable sides. Parameters `radius` (of the foot before its lobes, 3
modules), `height` (of the crest, 1 module), `rocks` (1 - 4, default 2),
`seed` and `segments` (columns around the crest, 8 - 40, default 18; the
rings a quarter of it and one); slots `grass` and `rock`; normals smooth with
hard edges at 38 degrees, so the hill is smooth and the rocks faceted; a
convex hull body; tags `walkable` and `ground`.

- A polar grid about a crest that stands 8 to 18 percent of the way toward
  one rim point, so that side is steep and the opposite one long; a lobed
  foot, a rounded crest, a straight flank and an easing foot, and a spur that
  lifts the flank along one direction.
- The rocks crop out of the steep side: pieces of the rock's boulder form
  sunk into the surface.
- The walkability check measures the steepest facet of the generated surface
  against the agent's slope; the derive range of the height keeps the steep
  side at most 43 degrees at every seed.

304 triangles at the defaults.

### Tree

`nature.tree` is a tree standing at the origin. Parameters `trunk_height`
(the canopy's base, 0.6 wall heights), `canopy_size` (its diameter, 2
modules), `species` (`broadleaf`, `conifer`, `bare`) and `seed`; slots `bark`
and `foliage`; a triangle mesh body; tags `obstacle` and `decoration`.

- A trunk of five rings with a lobed root flare, a bend and a taper, its base
  a fan about a vertex on the origin (slot `bark`), and by species:
  `broadleaf` - three or four limbs with an elbow, each carrying a clump of
  leaves, a crown clump on the trunk's top and two small clumps that break the
  outline; `conifer` - four or five tiers, each a skirt with a ragged drooping
  edge, hollow underneath; `bare` - four forked limbs and three small tufts of
  leaves (slot `foliage`). Every part is a closed solid and they overlap.

458 triangles at the defaults.

### Bush

`nature.bush` is a bush on the origin: a heart clump, a ring of four or five
lower clumps standing on the ground about it and two small clumps on top
(slot `foliage`), with bare twigs reaching past the leaves (slot `bark`).
Parameters `size` (its diameter, 1 module), `twigs` (1 - 6, default 3) and
`seed`; a convex hull body; tags `obstacle` and `decoration`. 408 triangles
at the defaults.

### Nature construction

The rock, land mass, hill, tree, bush, rock spire, leaf plant and palm shrub
generators run no convex hull or sweep, sort by no computed key, weld by no
distance and take no branch on a computed position: limits are `min` / `max`
of continuous terms, shared vertices are made once and reused by index, and
each draw is its own statement, so the order of the draws is the same with
every compiler. The plants and the rock spire draw from two streams of their
seed: one whose float draws only move vertices, and one named `... cells`
whose integer draws decide counts, windows and diagonals.
`Kit_nature_shapes.nature_topology_follows_from_integers_alone`,
`Kit_nature_plants.plant_topology_follows_from_integers_alone` and
`Kit_nature_rocks.rock_spire_topology_follows_from_integers_alone` hold the
facet lists equal across the corners of the float ranges.

### Cliff

`nature.cliff` (`kit_shapes_nature_terrain.cpp`) is a cliff segment along X
with its rock face toward +Z: parameters `length`, `height` (the flat top),
`depth` (from the back at z = -depth / 2 to the face's plane at +depth / 2),
`strata` (`massive`, `layered`, `blocky`), `overhang` (how far the face leans
out at the top, a share of the height, -0.25 - 0.25; default -0.1 and derived
within -0.2 - 0.05, so a cliff is battered: its base reaches further out than
its top), `roughness`, `ledges` (0 - 3) and `seed`; slots `cliff` (the face,
ends, back and base; Kit Dark Rock) and `grass` (the top); connectors start
and end (the base's end centers), base and top (the center of the walkable
top, between the back and the rim's nominal line); a triangle mesh body; seam
side -z (the flat back, for placing against a hill).

- Rows (`Cliff_row`, `Cliff_profile`) depend on the values alone: base rows
  about every 0.4 m, and a shelf - a row 0.06 m above another - at each ledge
  and, for layered strata about every 1 m and for blocky about every 1.6 m
  (at most eight), at each band boundary; at most 28 rows. Columns about
  every 0.35 m, at most 28. Two triangles per cell, the diagonal alternating.
- The nominal line, also of the values alone: the batter (the overhang), a
  talus foot (the lowest quarter of the height pushed out by up to 0.08 of
  the height, 0.2 - 1 m) and the ledge setbacks (0.4 m each), the setbacks
  and a backward lean scaled down together so the top keeps 1.5 m of depth.
- The relief, seeded from the stream "cliff face", scaled by the roughness
  and by the depth (0.4 - 1.2 m at roughness 1): bulges of 0.7 - 2.2 m radius
  (strongest for massive strata), each band stepping in or out (layered), each
  block of three columns by a band doing so (blocky), cracks (one column cut
  in) and buttresses (two columns standing out), and per-vertex noise.
- Joining rule: the end columns take no seeded relief and the next ones half,
  so a segment's end profiles depend on `height`, `depth`, `strata`,
  `overhang`, `ledges` and `roughness` alone; segments with those values equal
  join in a run without a gap, whatever their seeds and lengths.
- The top row's front rim wanders within 0.4 m of its nominal line; the top
  stays flat. Walkability (R2.10): the top's depth less the rim's 0.4 m is at
  least the agent's width at every value.
- The back, the base and the grass top are one quad per column; each end is a
  fan of triangles from its back bottom corner, which sees the whole outline.

At the defaults (height derived from seed 1: 6.9 m) the cliff has 428
triangles (budget 500); at its range maxima at most 2000.

### Shore and water

`nature.shore` (`kit_shapes_nature_terrain.cpp`) is a beach tile along X:
sand from a flat berm at its back edge (z = -run / 2) sloping down toward +Z
to a front lip 0.1 m high. Parameters `length`, `run`, `rise` (the berm's
height above the lip, fixed: walkability depends on it), `undulation`, `berm`
(the flat share of the run) and `seed`; slot `sand`; connectors start, end,
top (the back edge's center at the berm's height, facing -Z) and base; a
triangle mesh body; seam side -y; smooth shading.

- The sand is a height grid, a sample about every 0.5 m (2 - 32 per side),
  two triangles per cell. Its nominal profile is flat over the berm, then an
  S curve (`1 - (3q^2 - 2q^3)` over the slope) steepest halfway down and flat
  at the lip. The undulation adds two crossing waves of seeded heading and
  phase (wavelength 3 - 6 m, stream "shore undulation"), faded to zero at the
  four edges, so the ends and edges depend on the values alone and tiles
  join. At most 30 cells per side, which keeps the shore within the ceiling.
- The bottom is one quad; the back, front and ends are fans of triangles
  from a bottom corner (the sand falls toward the front, so a back corner sees
  each end's outline).
- Walkability (R2.10): the steepest slope, `atan(1.5 rise / slope run +
  2 pi undulation / 3)`, is at most the agent's maximum slope; the derive
  ranges keep a derived shore within 40 degrees.

At the defaults it has 238 triangles (budget 260).

`nature.water` is a flat rectangular water surface at y = 0 facing up, a
shape with `Kit_mesh_kind::surface` and `Kit_collision::none` (P5 of
`doc/plans/level_kit_extra_shapes.md`): parameters `width`, `depth` and
`subdivisions` (quads along each side, 1 - 31, fixed, within the ceiling; vertices for later
vertex effects) and `seed` (draws nothing); slot `water`; connectors left,
right, front and back (the edge centers); mirror planes x and z; 2
triangles at the defaults (budget 2). A piece of it has no physics body and
is not navigation input (`doc/editor/level_tools.md` "Kit brushes"), and it is no surface for snap to
surface or support (`doc/editor/level_tools.md` "Surface placement"): what is snapped from above passes
through it to the sand below. It is placed at the water level and other
pieces pass through it (U20): a shore slopes down under it, the shoreline
being where they meet, and a cliff stands in it.

### Palm

`nature.palm` (`kit_shapes_nature_plants.cpp`) is a single-trunk palm standing
at its origin. Parameters `species` (`coconut`, `date`, `royal`, `fan`,
`bottle`), `height` (the crown above the base), `bend` (the crown's
horizontal offset, a share of the height, 0 - 0.5), `fronds` (3 - 24),
`frond_length` (bounded to 0.1 - 0.6 of the height, so no leaf, fan rim or
dead leaf reaches the ground and no frond is too small for the mesh checks),
`droop`, `seed` and `segments` (trunk sides, 4 - 12); slots
`bark` (trunk, nuts, dead leaves) and `leaf` (fronds, fan leaves,
crownshaft); a triangle mesh body.

- The species sets the trunk's radius per meter of height, how much of the
  bend the trunk takes, the trunk's taper and rings (every second station
  wider), a green crownshaft (royal, bottle), the crown (feather fronds or
  fan leaves), the fronds' elevations from the lowest to the youngest, their
  droop factor, width and fold, and the nuts (coconut: three). Coconut: a
  slender curved trunk; date: a thick ringed trunk, stiff upright fronds;
  royal: a straight smooth column; fan: palmate leaves on stalks with a skirt
  of dead leaves (`bark`) below the crown; bottle: a trunk swollen low.
- The trunk is a `make_sweep()` of a regular polygon along a cubic Bezier that
  starts vertical (its base ring lies flat at y = 0) and leans toward a
  seeded heading (stream "palm trunk"); the crownshaft a short sweep along
  the trunk's end tangent; a nut a sweep tapering to a point.
- A feather frond is a folded leaf ("Leaf helper") along a cubic Bezier that
  leaves the crown at its elevation and sags by the droop; a fan leaf a
  pleated half disc on a stalk, a closed thin shell. The fronds go around the
  crown at the golden angle from a seeded heading (stream "palm fronds").
  `editor_level_tests` checks every species at the corners of its size
  parameters (least height with the longest, most drooping fronds, and the
  opposite corners) over four seeds: the base is the lowest point.

At the defaults (seed 1 derives height 11.1 m, bend 0.1, 20 fronds, droop
0.69) it has 1390 triangles (budget 1600); over derived seeds every species
keeps within the kit's ceiling (`editor_level_tests` records the counts).

#### Leaf helper

`add_leaf(builder, leaf, scratch)` (`kit_leaf.hpp`, Q2 of
`doc/plans/level_kit_more_shapes.md`) adds one leaf, frond or blade: a
`Kit_leaf` holds the spine (four cubic Bezier points, base to tip), the
section (`Kit_leaf_section::flat`, a thin lens; `thick`, a lens a third of its
width; `folded`, two thin halves sharing the midrib in an inverted V, the
leaflet rows of a feather frond; `folded_split`, the former build of
`folded`), the width, thickness, fold, the spine
parameters of the widest station and of the end of the widest stretch
(`widest_end`: an oblong blade; at most `widest_at`, one station), the base's
share of the width, the section's roll about the spine (`roll`: pi / 2 stands
the blade on edge) and the station count. Each part is a `make_sweep()` tapered from the base share to
the widest station and to a point, made in the generator's scratch mesh and taken in by
`Kit_mesh_builder::add_mesh()`, which adds a generated mesh's facets under
the current slot and splits each quad into two triangles (a bent sweep's
quads are not planar). `add_kit_sweep()` is the same for a sweep a generator
sets up itself.

The builder's other helpers (P4 of `doc/plans/level_kit_extra_shapes.md`):
`add_beam(a, b, width, height)` adds a box from a to b (a and b distinct, a
verified precondition), its width along the
axis' horizontal normal and its height along the normal above it (the
torii's tie beam and strut); `add_ring_solid(points, segments, bottom,
top)` closes rings of equal point counts with fans to two cap points (the
rock spire and ground patch), `add_ring_solid_paired_caps()` with strips (the
dune); `add_band(first, first_keys, second,
second_keys)` triangulates between two polylines whose keys rise along each,
advancing on the smaller next key (a closed ring repeats its first vertex a
turn later): the tower's rings of different corners.

`Kit_mesh_builder::write()` makes the facets in runs of equal corner count,
one resize of the mesh's attribute stores per run: at the defaults the palm
generates in about 4 ms (Debug, about the rock's), the cliff and shore in
under 0.5 ms.

### Leaf plant

`nature.leaf_plant` (`kit_shapes_nature_plants.cpp`) is a lush plant of large
leaves standing at its origin. Parameters `form` (`banana`, `taro`,
`strelitzia`, `fern`, `tree_fern`), `height` (0.3 - 8 m), `leaves` (3 - 14),
`droop` (0 - 1), `leaf_width` (0.1 - 0.6) and `seed`; height, leaves and droop
in the default mask; slots `bark` (stems and dead leaves), `leaf` and
`young_leaf` (the youngest leaves); no connectors; no body (`Kit_collision::none`,
P5 of `doc/plans/level_kit_extra_shapes.md`: soft dressing a walker passes
through; its pieces carry `pass_through`). It shares its parts with the palm
shrub ("Plant parts"). By form:

- `banana`: a tapering stem, broad oblong blades with a V fold at the base
  that arch over and twist, a rolled new leaf standing in the crown (slot
  `young_leaf`) and one or two dry leaves hanging down the stem (slot `bark`).
- `taro`: heart-shaped blades hanging from the tips of long stalks that rise
  from a mound, each blade's halves raised about its midrib.
- `strelitzia`: paddle blades on long stalks in one fan, alternately to
  either side, leaning out more the older they are.
- `fern`: arching fronds of nine pairs of leaflets (seven above ten fronds)
  about two or three unrolling croziers (slot `young_leaf`).
- `tree_fern`: the fern's crown on a trunk with two or three dead fronds
  hanging under it (slot `bark`).

`leaf_width` is the blade's width over its length (banana, strelitzia, taro)
and the leaflets' length (ferns); `droop` turns the leaves' ends down. 488
triangles at the defaults, at most 1652 (a tree fern with every parameter at
its maximum).

#### Plant parts

The leaf plant and the palm shrub share their parts, all closed, overlapping
solids:

- a leaf's midrib is an arc in a vertical plane whose direction turns from
  one elevation to another (`Leaf_arc`). `fit_leaf_arc()` scales the arc's
  descents by the largest share that keeps its tip above a given height, so a
  drooping leaf rests above the ground at every value;
- a ribbon (`add_ribbon()`, `add_blade()`): at each station the two edges, the
  midrib and the keel under it, pointed at both ends; the edges' lift over the
  midrib is the leaf's fold. A blade is a stalk widening into an oblong blade;
- a shell (`add_shell()`): a fan of triangles from a hub over a rim, its copy
  underneath and walls between. A palm's fan (`add_fan()`) has fingers that
  are ridges from the hub to their tips between dropped clefts; a taro's blade
  (`add_heart()`) is a closed rim of ten points. `flatten_above()` scales a
  shell's heights about its hub (one affine map) where it would reach the
  ground;
- a frond (`add_frond()`): a three-sided rachis and pairs of leaflets, each a
  low tetrahedron whose ridge folds it;
- a stem (`add_stem()`): rings from the ground up, the base a fan about a
  vertex on the origin.

Leaf k of n has a rank from 0 (oldest, lowest, drooping most) to 1 (youngest,
standing); leaves follow each other by the golden angle.

### Rock spire

`nature.rock_spire` (`kit_shapes_nature_rocks.cpp`) is a standing rock on a
flat base at its origin. Parameters `form` (`pinnacle`, `stack`, `hoodoo`,
`mesa`), `height` (2 wall heights), `radius` (the base's, 1 module), `taper`
(how much narrower the top is), `lean` (0 - 15 degrees), `roughness`,
`strata` (beds of rock between the foot and the top, 4 - 16, default 9),
`seed` and `segments` (columns, 6 - 20); height, radius and lean in the
default mask; slots `rock` and `grass` (the ledge tops and a flat top);
connector base; a triangle mesh body.

- One grid of `segments` columns and rows from a flaring foot to the top, two
  triangles a cell with the diagonal by an integer draw. The rows are the
  foot, the beds of uneven thickness (alternately harder and softer, so they
  stand out or are worn back), a shelf row after two or three of them, and
  the hoodoo's cap rock. A ledge is a window of two or more columns whose face
  is set back above its shelf.
- Every column has its own plan radius (a lobed plan, about one column in
  three a groove), a shoulder above which it is set back, and a vertical
  shift (a dip plus faults between column blocks) that every row takes as a
  share of the gap to its nearer neighbour row, so beds never cross and no
  seam runs around the rock at one height. The shaft leans by `lean` and its
  upper half bends aside. `Kit_nature_rocks.rock_spire_rows_rise_up_every_column`
  holds the rows' order at the corners of the ranges.
- Forms: `pinnacle` (three ledges, a point off the top ring's center),
  `stack` (a wave-cut notch on one side of the foot, a turf top), `hoodoo` (a
  neck under a wider cap rock), `mesa` (a talus foot, a flat turf top at the
  height).
- One to five fallen blocks, by the form and an integer draw, lie half sunk in
  the foot.
- Replace carries `roughness` to and from `nature.rock`; the forms take
  their default (the rock's form names differ).

396 triangles at the defaults, at most 980 (a hoodoo with every parameter at
its maximum).

### Basalt columns

`nature.basalt_columns` (`kit_shapes_nature_rocks.cpp`) is a cluster of
hexagonal columns in a round footprint at its origin. Parameters `radius`
(1.5 modules), `column_width` (across the flats, 0.5 modules), `height` (the
highest top, 1 wall height), `height_spread` (the share of the height the
lowest tops stand below it, 0 - 0.9), `step` (the height quantum, 0.3 m;
fixed: walkability depends on it) and `seed`; radius and height spread in
the default mask; slot `rock`; connector base; a triangle mesh body; tags
`walkable` and `rock`.

- The columns stand on a hexagonal lattice inside the footprint (the center
  cell always), each 2 - 5 cm in from its cell (joints, so neighbours share
  no vertex); a lattice of more than 72 columns widens its spacing, which
  keeps the shape within the ceiling.
- Each top lies a whole number of steps below the height: a smooth field
  rising toward a seeded side and toward the middle, with a wave and a little
  noise (stream "basalt columns"), then lowered until neighbouring tops
  differ by at most one step: terraces.
- Walkability (R2.10): with the step at most the agent's climb the tops are
  stairs among themselves; the outer columns stand from the ground at their
  full height.

At the defaults (seed 1 derives a radius of 1.75 m) it has 740 triangles
(budget 760), about 1.1 ms; the most is 1220.

### Palm clump and palm shrub

`nature.palm_clump` (`kit_shapes_nature_plants.cpp`) is several slender
stems from one root ball at its origin. Parameters `species` (`areca`:
ringed canes and arching feather fronds; `rhapis`: thin canes with fan
leaves; `nipa`: long erect fronds from short stems at the ground), `stems`
(1 - 9), `height` (the tallest stem's crown, 1.2 wall heights), `spread`
(how far the outer stems lean out), `fronds` (per stem, 2 - 10) and `seed`;
stems and height in the default mask; slots `bark` and `leaf`; a
triangle mesh body.

- Each stem is the palm's trunk and crown (`add_palm_tree()`, shared with
  `nature.palm`) with a lighter crown: five trunk sides, three stations per
  frond, five sectors per fan leaf, no skirt. The first stem stands in the
  middle at the full height, the others around it (stream "palm clump
  stems") at 0.55 - 0.95 of it, leaning out by the spread. The clump has at
  most 40 fronds in all, which keeps it within the ceiling: nine stems carry
  four or five each.
- No frond's or fan's control point lies lower than its own reach below its
  spine above the ground, so a low clump's fronds rest above it.

`nature.palm_shrub` is a trunkless crown at the ground at its origin.
Parameters `species`, `size` (1.2 m), `fronds` (3 - 16) and `seed`; size and
fronds in the default mask; slots `bark` (the stem and dead leaves),
`leaf` and `young_leaf` (the youngest leaves); a convex hull body. It shares
its parts with the leaf plant ("Plant parts"). By species:

- `palmetto`: pleated fans of eight pointed fingers on long stalks, the young
  ones standing and the old ones low and drooping, a spear leaf in the middle
  (slot `young_leaf`) and one or two dead fans on the ground (slot `bark`).
- `cycad`: a scaly barrel carrying a rosette of arching feather fronds of
  eleven leaflet pairs standing in a V (eight above twelve fronds) about a
  flush of two or three new fronds (slot `young_leaf`).
- `seedling`: pleated undivided wedge leaves of four fingers on a thin stem
  about a spear.

- Replace carries `height` and `fronds` (clamped) between the palm and the
  clump; the species take their default.

At the defaults the clump (seed 1: eight areca stems of 3.9 m) has 1672
triangles (budget 1940), about 4 - 6.5 ms, the most 1708; the shrub 1246,
at most 1778 (a cycad with every parameter at its maximum).

### Dune and ground patch

`nature.dune` (`kit_shapes_nature_terrain.cpp`) is an elongated sand mound
along X. Parameters `length` (10 m), `width` (5.5 m) and `height` (0.9 m; all
in meters, because the lee's slope bounds the height by the width at every
level length), `asymmetry` (how far the crest lies toward the lee, +Z, a
share of the half width, 0 - 0.8), `seed` and `segments` (points around,
8 - 48; an odd count uses one less); length, width and height in the default
mask; refused combination (`check_combination`): a length under 1.04 times
the width (the derive ranges keep to it; a Replace whose carried values give
one is refused); slot `sand`;
smooth shading with hard edges above 30 degrees (the brink stays sharp); a
convex hull body; tag `walkable`.

- Nine rings from the rim to a narrow flat crest, each a stadium: straight
  sides of the length less the width between half
  circles whose radius shrinks by the half width over the rings, the center
  drifting toward the lee by the asymmetry and along X by a seeded share (at
  most 0.1 of the half width), the radius scaled by a seeded outline of up to
  6 percent that fades out at the rim and the crest. The profile eases in at
  the foot and over the crest, straight between (its steepest rate 1.25).
- The base and the crest are strips of quads pairing each point with its
  mirror across x = 0 (`Kit_mesh_builder::add_ring_solid_paired_caps()`):
  a fan from one point over a long narrow ring makes slivers.
- Walkability (R2.10): the next ring lies inside each ring by at least the
  half width times (0.94 less the drift), so the steepest slope is
  `atan(1.25 height / (0.95 x that distance))`; `editor_level_tests` checks
  that no facet of a dune the check passes is steeper than the agent's slope.

`nature.ground_patch` is a thin irregular patch lying on other ground.
Parameters `size` (3 modules), `thickness` (the edge's height, 0.01 - 0.4 m:
at most the agent's climb; fixed), `irregularity` (0 - 0.6), `dome` (0 - 0.1
of the size), `seed` and `segments` (24); size and irregularity in the
default mask; slot `earth` (Kit Sand and Kit Grass among its alternatives);
smooth shading; a convex hull body; tag `walkable`.

- Three rings: the outline at y = 0, a bevelled edge at the thickness (0.97
  of it) and the dome's ring (0.55 of it), then the dome's top. The outline
  is 1 plus harmonics 2, 3 and 4 of seeded amplitudes falling with their
  order, scaled to the irregularity.
- Walkability: the edge is at most the climb; the slope is measured on the
  patch's own top triangles (the outline varies between neighbouring
  points, so no closed form bounds it).

At the defaults the dune has 284 triangles (budget 290), about 0.3 ms, the
most 860 (48 segments); the patch 144 (budget 150), about 0.2 ms, the most
288.

### Succulent

`nature.succulent` is a rosette of thick pointed leaves in a spiral at its
origin: the old leaves low and wide, the young ones upright about a bud of
unopened leaves. Parameters `form` (`agave`, `aloe`, `yucca`), `size` (twice
the longest leaf: a rosette's width, a yucca's height; 1 m, 0.2 - 4),
`leaves` (16, 6 - 32), `spread` (the angle of the outermost leaves from the
upright, 65 degrees, 10 - 85) and `seed`; size and leaves in the default
mask; slots `foliage`, `bark` (the leaves' dry points, a yucca's trunk and
dead leaves) and `shoot` (the youngest fifth of the leaves and the bud);
faceted; a convex hull body.

- A leaf is one closed part: rings of its section along an arc that turns
  from its elevation at the stem to the one at its tip, closed to a point.
  The leaves follow the golden angle around the axis from the oldest,
  outermost one to the youngest; their lengths, turns and elevations vary by
  seed. An arc that would end below the ground descends by the share that
  keeps it above it, so no leaf passes through the ground at any spread.
- `agave`: broad channelled leaves (0.28 of their length wide, the edges
  raised over the midrib) that bend back a little, each ending in a dark
  spine (slot `bark`), on a low core.
- `aloe`: thick tapering leaves curving up and in, their last tenth dry
  (slot `bark`).
- `yucca`: a trunk of half the size, leaning by seed, carrying a head of two
  thin straight swords for each of the `leaves`, from below the level to the
  upright; the oldest quarter hang dead down the trunk (slot `bark`).
- Topology follows from the form and the leaf count alone; no seed changes
  it.

At the default values (16 leaves) an agave has 702 triangles: 40 for each
leaf and 62; with the default mask's leaves and size of seed 1 the shape has
582 (budget 600). The most is 1626 (a yucca of 32 leaves: 24 for each of its
64 swords).

### Grass tuft

`nature.grass_tuft` is a tuft of blades on a tussock at its origin.
Parameters `form` (`grass`, `reed`, `pampas`), `height` (of the tallest
stalk, 0.6 m, 0.1 - 3), `blades` (16, 4 - 40), `spread` (how far the blades
lean and arch out, 0.5, 0 - 1) and `seed`; height and blades in the default
mask; slots `blade` (the blades and the tussock) and `plume` (ears, panicles and plumes; Kit Straw);
faceted; no body (`Kit_collision::none`): a walker passes through it and it
is no navigation input.

- A blade is one closed part of three-point rings (its two edges and its
  keel) along an arc, closed on the ground under its foot and to a point at
  its tip; the blades stand in a spiral by the golden angle, the inner ones
  upright, the outer ones leaning and arching over. An arc that would end
  below the ground descends by the share that keeps it above it.
- `grass`: arching blades and one stalk for every ten blades and one, each
  ending in an ear.
- `reed`: a cane for every three blades (at least two) in a loose stand,
  each ending in a panicle that droops to one side; the blades are the
  leaves standing off the canes, shared out over them level by level.
- `pampas`: a fountain of long blades that arch to the ground under two
  tall plumes and one for every six blades, their stalks bowing over.
- Topology follows from the form and the blade count alone.

With the default mask's blades and height of seed 1 it has 614 triangles
(budget 640); the most is 1952 (a reed of 40 blades).

### Boulder cluster

`nature.boulder_cluster` is a heap of boulders in a round footprint.
Parameters `radius` (of the footprint, 2 modules, 0.3 - 16), `count` (5,
2 - 10), `size_spread` (how much smaller the smallest boulder is than the
largest, 0.5, 0 - 1), `form` (`angular`, `worn`, `mixed`; default mixed),
`roughness` (0.6, 0 - 1) and `seed`; radius and count in the default mask;
slot `rock`; faceted; a triangle mesh body.

- The largest boulder stands over the origin; the others follow the golden
  angle around it, each smaller than the last, leaning on its flank out to
  the footprint's edge; every third one, a smaller one, lies tilted on the
  largest one's shoulder. The parts are closed and overlap.
- A boulder is a stack of rings of one convex outline (5 - 7 sides by an
  integer draw) whose edges keep their directions from ring to ring, so
  every side face is a planar trapezoid, with a chamfer down each corner
  that is planar too. One affine map dips its strata and leans it; the base
  ring is where the lowest band's planes meet the ground. Some sides are one
  fracture plane from foot to top (an integer draw per side).
- `angular`: 5 - 6 sides, narrow chamfers, a ledge above the foot, every
  other side a fracture plane, a tilted flat top face. `worn`: 5 - 7 sides,
  wide chamfers, an undercut foot, a rounded profile to a low apex. `mixed`:
  each boulder one or the other by an integer draw.
- Topology follows from the count, the form and the seed's integer draws
  (side counts, fracture sides, forms) alone.

With the default mask's radius and count of seed 1 it has 332 triangles
(budget 340); a boulder has at most 138 (a worn one of seven sides), so the
most is 1380.

### Stepping stone

`nature.stepping_stone` is a flat-topped stone to step on. Parameters
`diameter` (its width before the outline's variation, 0.9 m, 0.3 - 4),
`thickness` (the height of its level top, 0.25 m, 0.05 - 0.5), `sides` (of
the outline, 7, 5 - 9) and `seed`; the diameter in the default mask; slot
`rock`; faceted; connectors base and top (the top face's height); a convex
hull body; tag `walkable`.

- The boulder's construction with a level top: a convex outline of the
  sides with a chamfer down each corner, undercut at the ground, upright to
  0.8 of the thickness and bevelled to a top face of 0.9 of the footprint;
  about one side in four is a fracture plane from foot to top. The top is
  one polygon at exactly the thickness.
- A thickness within the agent's climb (0.4 m) is a step up; the derive
  range keeps to it. A stone is narrower than the agent's standing room by
  intent, so it states no walkability check.
- Topology follows from the sides alone: 6 facets a side and 2.

At the defaults it has 108 triangles (budget 120); the most is 140 (9
sides).

### Bamboo

`nature.bamboo` is a clump of jointed canes. Parameters `height` (of the
tallest cane, 1.5 wall heights, 1 - 16 m; in the default mask), `culms` (3 -
16, default 6; in the default mask), `radius` (of the canes' feet, 0.5
modules, 0.1 - 4 m), `lean` (outward lean of the outermost canes, 5 degrees,
0 - 20; in the default mask), `cane_width` (of the thickest cane at its foot,
0.1 m, 0.03 - 0.3) and `seed`; slots `cane`, `bark` (the nodes) and
`leaf`; no connectors; a triangle mesh body; seeded.

- The tallest cane stands at the origin; the others stand around it on a
  golden-angle spiral out to the radius, 0.55 - 0.95 of the height, leaning
  outward and arching further toward their tips.
- A cane is a stack of level rings (so every side is a plane): the foot, a
  ring under and over each node, and the tip, tapering toward the tip. The band over a node is slot `bark`, wider below than above, which
  draws the joint.
- A spray is a twig (a level triangle under a point, slot `cane`) with leaves
  along it to the left and the right and one from its tip; a leaf is five
  points: a flat top over a keel, pointed at both ends, drooping. One spray
  stands on the cane's tip, the others leave the nodes below it, two to a node
  on opposite sides.
- The more canes, the fewer sides, nodes and leaves each takes, so every count
  stays within the ceiling: up to 4 canes 6 sides, 8 internodes and 7 sprays
  of 4 leaves (384 triangles a cane); up to 8 canes 5, 6 and 5 sprays of 3
  (226); up to 12 canes 4, 5 and 3 sprays of 3 (142); more 3, 5 and 3 sprays
  of 3 (122).

Derive ranges: `height` 1 - 4 wall heights (0.25), `culms` 3 - 16, `radius`
0.3 - 1 modules (0.1), `lean` 0 - 10 degrees (1), `cane_width` 0.07 - 0.14 m
(0.01).

At the defaults (seed 1 derives 3 canes) it has 1152 triangles (budget 1200);
six canes have 1356; the most is 1952 (16 canes).

### Rock arch

`nature.rock_arch` is a natural arch across X over its origin, walked through
along Z. Parameters `span` (the clear width at the ground, 3 m, 1 - 12),
`rise` (the clear height at the center, 3 m, 1 - 10; both in meters, because
the agent fixes the least opening at every level length), `thickness` (of the
rock over the crown, 1.2 m, 0.4 - 4; in the default mask), `depth` (along Z
at the crown, 1.6 m, 0.5 - 6), `roughness` (0.6), `seed` and `segments`
(stations along the arch, 8 - 24, default 14); slot `rock`; connectors start,
end and base; a triangle mesh body; seeded; tag `opening`.

- The opening's outline is `make_kit_arch_head_points()` ("Shared construction") with the seeded
  jitter, drawn outside the stated half ellipse by the jitter and the chords'
  sag, so no vertex stands inside the ellipse of the span and the rise.
- The rock is one band of sections of ten points from the right foot over the
  crown to the left foot. A section spans from the opening's outline to an
  outer outline that is a squarish quarter on each side (one leg heavy, 1.5 -
  2 thicknesses, the other 0.9 - 1.3; one shoulder square, the other worn);
  it is a squarish ring, deeper at the feet, whose point on the opening's
  outline stays and whose other points stand in or out by the roughness. The
  feet's sections lie on the ground.
- The start (-X) and end (+X) connectors lie on their own foot's outer edge,
  each at its side's seeded reach: the connectors and the generator draw the
  opening's outline and the two sides from the same seed.
- Four loose rocks (two rings of six under a point) lie before and behind the
  feet.
- Walkability (R2.10): the clear width of the stated ellipse at the agent's
  height plus the margin, and the rise, pass the opening check.

Derive ranges: `span` 2.5 - 5 m (0.25), `rise` 2.6 - 4 m (0.2), `thickness`
0.8 - 2 m (0.1), `depth` 1.2 - 2.6 m (0.2), `roughness` 0.3 - 0.9 (0.05).

At the defaults it has 396 triangles (budget 420): 20 a station, 20 for the
feet and 24 a loose rock; the most is 596 (24 stations).

### Log

`nature.log` is dead wood at its origin. Parameters `form` (`log`, `stump`,
`driftwood`; default `log`), `length` (2 m, 0.3 - 8; a stump is 0.4 of it
high), `width` (the trunk's, 0.4 m, 0.1 - 2), `bend` (0 - 1, default 0.3: how
far a lying trunk bows toward -Z at its ends, 0.15 of the length at 1, and how
far a stump leans), `stubs` (branch stubs, 0 - 4, default 2), `seed` and
`segments` (points around the trunk, 6 - 14, default 10); slots `bark`,
`sapwood` and `heartwood`; connectors base and top (over the origin: on a lying
trunk's upper side, at the height of a stump's cut); a convex hull body;
seeded; tags `decoration` and `wood`.

- `log`: a trunk along X of nine rings, a little thicker toward -X, each point
  a little in or out. It is pressed into the ground: its two lowest lines of
  points lie in the ground plane, so the base is flat under the whole trunk.
  Each end is a cut on a slant showing the bark's edge (slot `bark`), the
  sapwood and the heartwood about a pith off the center.
- `stump`: a trunk of five rings on the ground, every second line of points a
  root that reaches out at the foot by its own share, the trunk leaning by the
  bend. The top is a cut on a slant with the same three rings of colour.
- `driftwood`: a barkless trunk (slot `sapwood`) that is full about its middle
  and thins toward both ends, which rise off the ground, wander aside and end
  in broken points (slot `heartwood`); a patch of bark is left on its upper
  side.
- Stubs leave a lying trunk on its upper half, to the two sides in turn and
  leaning toward the nearer end; they leave a stump outward and upward. A stub
  has a collar where it leaves the trunk and a cut end (bark around sapwood);
  driftwood's stubs are longer and end in a point.
- The trunk is one closed part and every stub one more.

Derive ranges: `form` all values, `length` 0.5 - 4 m (step 0.25), `width`
0.2 - 0.8 m (0.05), `bend` 0 - 0.6 (0.05), `stubs` 0 - 4. `length`, `width` and
`bend` are in the default mask.

At the defaults it has 360 triangles (budget 380); the most is 564 (a log of
fourteen points with four stubs).

### Shell

`nature.shell` is sea life left on the sand at its origin. Parameters `form`
(`conch`, `scallop`, `starfish`; default `conch`), `size` (a conch's length, a
scallop's width, a starfish's width, 0.3 m, 0.05 - 2), `ribs` (5 - 12, default
8: a scallop's ribs, a starfish's arms, the knobs around a conch's whorl) and
`seed`; slots `shell` and `coral`; no connectors; no body (`Kit_collision::none`,
P5 of `doc/plans/level_kit_extra_shapes.md`): a walker passes through it and it is no navigation input; seeded; tags
`decoration` and `beach`.

- `conch`: rings of 2 * `ribs` points about an axis along X from the spire's
  point at -X over three stepped whorls with knobs on their shoulders to the
  body whorl and the canal's point at +X. The whorls lean, the outer lip flares
  up and toward +Z, and the strips of the body whorl under the lip are the
  aperture (slot `coral`), drawn in a little. The body rests on its two lowest
  lines of points, which lie in the ground plane. 40 * `ribs` triangles.
- `scallop`: the upper valve over a flat underside: rays from the umbo at the
  back (-Z) to the rim, a furrow and a rib in turn, the rays' lengths those of
  a circle through the umbo, so the rim is round and scalloped; two flat ears
  along the hinge end at a notch beside the fan. Every third rib is a band of
  slot `coral`.
- `starfish`: `ribs` arms about a disc, flat below, with a ridge along each
  arm; every arm has its own length and turns aside toward its tip, which lifts
  a little. Three knobs stand on every ridge and one on the middle (slot
  `shell`; the body is slot `coral`).

Derive ranges: `form` all values, `size` 0.1 - 0.6 m (step 0.05), `ribs`
5 - 12. `size` is in the default mask.

At the defaults it has 320 triangles (budget 340); the most is 606 (a starfish
of twelve arms).

### Blocky tree

`nature.blocky_tree` is a tree of boxes at its origin. Parameters `form`
(`cube`, `tiers`, `cluster`; default `cube`), `trunk_height` (the trunk under
the canopy, 0.6 wall heights, 0.3 - 8 m), `canopy_size` (the canopy's width,
2 modules, 0.5 - 8 m), `blocks` (canopy blocks, 1 - 4, default 3) and `seed`;
slots `bark`, `foliage` and `shoot`; no connectors; a triangle mesh body;
seeded; tags `decoration` and `tree`.

- Every part is a box turned about the upright: 12 triangles. The trunk is
  square, on a wider root block with one smaller root against a side.
- A canopy block is four boxes: the block, a lighter block on its top toward
  one side (slot `shoot`), and two buds standing half out of two faces next to
  one another.
- `cube`: a cube on the trunk (the whole canopy size with one block, 0.6 of it
  otherwise) and smaller cubes around it a third of a turn apart, each higher
  than the one before; the first hangs under the main cube's foot.
- `tiers`: slabs on one another, each 0.21 of the canopy size smaller than the
  one below and turned against it: a blocky conifer.
- `cluster`: a cube on the trunk and one on a branch (a beam of slot `bark`
  from the trunk) for each further block, at its own height around the trunk.
- The parts are 3 + 4 * `blocks` boxes, and a branch for each block of a
  cluster but the first.

Derive ranges: `form` all values, `trunk_height` 0.4 - 0.8 wall heights (step
0.05), `canopy_size` 1.5 - 3 modules (0.25), `blocks` 1 - 4. `trunk_height`,
`canopy_size` and `blocks` are in the default mask.

At the defaults it has 132 triangles (budget 280, the most a seed derives); the
most is 264 (four cubes of a cluster).

## Prop kit

`kit_shapes_extra_01_08.cpp` and `kit_shapes_extra_09_16.cpp`.

### Crate

`prop.crate` is a slatted wooden crate at its origin. Parameters `width`,
`height` and `depth` (each 0.75 modules, 0.3 - 4 m), `slats` (slats on a side
and planks in the lid, 2 - 5, default 3), `lid` (`closed`, `open`, `none`;
default `closed`) and `seed`; slot `wood`; connectors base and top; a box
body with the lid closed or left out, a convex hull body with it open; mirror
plane x; tags `prop`, `crate` and `stack`.

- Every part is a box: two skids along Z under a floor, four corner posts that
  stand out of the sides, and `slats` level slats on each of the four sides
  with a gap between two, their ends inside the posts. The crate has no inner
  lining: the gaps are open.
- `closed`: a lid of `slats` planks along X lies on the posts under two battens
  along Z, whose tops are the height. `open`: the same lid stands on its back
  edge (-Z), 70 degrees up from closed, over the height; the top connector is
  on the rim of the posts. `none`: the posts and slats reach the height.
- The bounds are the width by the depth; the parts are 7 + 4 * `slats` boxes,
  and `slats` + 2 for a lid.
- Crates stack base on top.

Derive ranges: `width`, `height` and `depth` 0.5 - 1.25 modules (step 0.25),
`slats` 2 - 5, `lid` all values. `width`, `height` and `depth` are in the
default mask.

At the defaults it has 288 triangles (budget 300); the most is 408 (five slats
under a lid).

### Barrel

`prop.barrel` is a wooden barrel at its origin. Parameters `height` (from head
to head, 0.9 m, 0.4 - 2), `radius` (of the staves at the middle, 0.3 m,
0.15 - 1), `bulge` (how much narrower the ends are than the middle, 0 - 0.2,
default 0.12), `hoops` (2 - 4, default 4), `pose` (`upright`, `lying`; default
`upright`), `seed` and `segments` (staves around the barrel, 8 - 24, default
12); slots `wood` and `metal`; connectors base and top; a convex hull body;
mirror plane z; tags `prop` and `barrel`.

- One closed part: rings of `segments` points from head to head, a flat side
  to a stave. The staves bulge along a parabola between two heads that lie a
  little inside the chime. A hoop (slot `metal`) is three bands standing out of
  the staves: two hoops lie toward the ends, three add one about the middle,
  four are two pairs.
- `upright`: it stands on its lower chime. `lying`: its axis is along X and it
  rests on a side of its widest ring, between two chocks (wedges of slot
  `wood`) at +Z and -Z; the top connector is on its upper side, the widest
  ring's highest point (a side at an even `segments`, a point at an odd one).
- 2 * `segments` * (12 + 3 * `hoops`) + 2 * (`segments` - 2) triangles, and 16
  for the chocks.

Derive ranges: `height` 0.7 - 1.2 m (step 0.05), `radius` 0.25 - 0.4 m (0.05),
`bulge` 0.06 - 0.18 (0.02), `hoops` 2 - 4, `pose` all values. `height` and
`radius` are in the default mask.

At the defaults it has 596 triangles (budget 620); the most is 1212 (a lying
barrel of 24 staves with four hoops).

### Pot

`prop.pot` is a vessel at its origin, hollow at its mouth. Parameters `form`
(`jar`, `amphora`, `urn`, `bowl`, `planter`; default `jar`), `height` (0.6 m,
0.15 - 3), `width` (the widest diameter, 0.5 m, 0.15 - 3), `neck` (the mouth's
width in widths, 0.3 - 0.9, default 0.6), `seed` and `segments` (8 - 20,
default 12); slot `stone`; connectors base and top; a convex hull body; mirror
plane z; tags `prop` and `pot`.

- One closed part turned about the upright: rings from the foot over the rim
  down into the hollow, whose floor lies under the rim.
- `jar`: a belly under a short neck and a rim. `amphora`: a belly on a small
  foot under a long neck and a wide lip, with two handles of two beams each
  toward +X and -X. `urn`: a belly on a pedestal foot under a neck and a rim.
  `bowl`: wide and open, rounded. `planter`: a tapered tub under a rim band,
  filled to under the rim.
- `neck` sets the mouth of a jar, an amphora and an urn; a bowl and a planter
  are open at their full width.

Derive ranges: `form` all values, `height` 0.3 - 1.2 m (step 0.1), `width`
0.3 - 0.8 m (0.1), `neck` 0.3 - 0.9 (0.1). `height` and `width` are in the
default mask.

At the defaults it has 236 triangles (budget 250); the most is 524 (an amphora
of 20 points).

### Table

`prop.table` is wooden furniture at its origin, filling its width, depth and
height. Parameters `form` (`table`, `bench`, `stool`, `shelf`; default
`table`), `width` (1.4 m, 0.3 - 4), `depth` (0.8 m, 0.3 - 2), `height` (0.75 m,
0.3 - 3), `legs` (`posts`, `trestles`; default `posts`) and `seed`; slot
`wood`; connectors base and top; a triangle mesh body; mirror plane x; tags
`prop`, `furniture` and `table`.

- Every part is a box; a splayed leg has a level foot and a level head.
- `table`: a top on legs. `bench`: a seat of two planks at 0.55 of the height,
  on legs, before a back of two uprights and two slats at -Z. `stool`: a seat
  on four splayed legs with four rungs, or (`trestles`) on two boards with a
  stretcher. `shelf`: four shelves between four posts, or (`trestles`) between
  two side boards before a back board.
- Legs of a table and a bench: `posts` are four posts under four aprons;
  `trestles` are two trestles of two splayed legs under a tie, joined by a
  stretcher.

Derive ranges: `form` all values, `width` 1 - 1.8 m (step 0.2), `depth`
0.6 - 0.9 m (0.1), `height` 0.7 - 0.8 m (0.05), `legs` all values. No parameter
is in the default mask.

At the defaults it has 108 triangles (budget 110); the most is 168 (a bench on
posts).

### Market stall

`prop.market_stall` is a market stall at its origin, its counter toward +Z.
Parameters `width` (2 modules, 1.2 - 8 m), `depth` (between the posts,
1 module, 0.6 - 4 m), `height` (0.8 wall heights, 1.8 - 6 m), `awning` (`flat`,
`sloped`, `peaked`; default `sloped`), `overhang` (0.3 m, 0 - 0.8) and `seed`;
slots `wood` and `cloth`; connectors base, start and end (at the base's sides,
facing -X and +X); a triangle mesh body; mirror plane x; tags `prop`, `market`
and `stall`.

- Four posts stand at the corners, a rail joining the front two and the back
  two under the cloth. The counter is a top at 0.9 m (at most 0.45 of the
  height) on a front and two sides of boards.
- The awning (slot `cloth`) is a slab the width wide that reaches `overhang`
  beyond the posts at the front and the back: `flat` level at the height;
  `sloped` falling from the height at the back by 0.2 of the height to the
  front; `peaked` two slopes falling by 0.18 of the height from a ridge along X
  at the height. A valance hangs flush from its front edge. The cloth and the
  valance are one solid: a prism along X whose section's end caps are convex
  quads sharing their edges (`Kit_mesh_builder::add_x_prism()`).
- The sides are the width apart: stalls join side to side by end and start.
- The parts are 10 boxes and the awning.

Derive ranges: `width` 2 - 3 modules (step 0.5), `depth` 1 - 1.5 modules
(0.25), `height` 0.75 - 0.9 wall heights (0.05), `awning` all values,
`overhang` 0.2 - 0.5 m (0.1). `overhang` is in the default mask.

At the defaults it has 140 triangles (budget 150); the most is 148 (a peaked
awning).

### Cart

`prop.cart` is a two-wheel hand cart along Z, its axle over the origin.
Parameters `length` (from the bed's back to the handles' ends, 1.5 modules,
0.8 - 4 m), `width` (over the wheels, 1 module, 0.5 - 2.5 m), `wheel_radius`
(0.4 m, 0.2 - 0.8), `tilt` (0 - 15 degrees, default 8), `sides` (`open`,
`rails`, `boards`; default `rails`), `seed` and `segments` (points around a
wheel, 8 - 16, default 12); slots `wood` and `metal`; connector base; a
triangle mesh body; mirror plane x; tags `prop` and `cart`.

- Two shafts run under a deck from 0.3 of the length behind the axle to 0.7 of
  it before, where they are the handles. The bed tilts about the axle toward
  the handles by `tilt`, at most what keeps the handles' ends over the ground,
  and stands on two legs under its front at every tilt; the legs rise from the
  ground into the deck's middle, so they meet the deck at every tilt.
- The wheels stand on the ground the width apart: a disc of wood between a hub
  and a tyre of slot `metal`, on an axle of slot `metal`.
- `rails`: two posts and a rail on each side. `boards`: a board on each side
  and across both ends. `open`: the deck alone.
- The parts are 8, 14 with rails and 12 with boards.

Derive ranges: `length` 1.5 - 2 modules (step 0.25), `width` 1 - 1.25 modules
(0.25), `wheel_radius` 0.3 - 0.5 m (0.05), `tilt` 0 - 15 degrees (1), `sides`
all values. `tilt` is in the default mask.

At the defaults it has 520 triangles (budget 540); the most is 648 (rails and
wheels of 16 points).

### Well

`prop.well` is a well at its origin. Parameters `radius` (of the wall's outer
face, 0.75 modules, 0.4 - 3 m), `wall_height` (0.9 m, 0.4 - 1.5), `roof`
(`none`, `crossbar`, `gable`; default `crossbar`), `seed` and `segments`
(points around the wall, 8 - 20, default 12); slots `stone` and `wood`;
connector base; a triangle mesh body; tags `prop` and `well`.

- The ring wall (slot `stone`) is one part: from its foot up to a coping that
  stands out of both faces, and down the shaft to a floor at 0.15 of the wall
  height. A wooden bucket stands on the coping toward +Z at every roof.
- `crossbar`: two posts beside the wall at +X and -X carry a beam, 1.4 radii
  (0.9 - 2.2 m) over the wall, over a windlass with a crank beyond the post at
  +X. `gable`: the same under a roof of two slopes sharing a ridge along X
  (one prism), half the radius and a post over the beam, on two king posts.
- The parts are 2, 8 with a crossbar and 11 with a gable.

Derive ranges: `radius` 0.6 - 1 modules (step 0.05), `wall_height` 0.7 - 1 m
(0.1), `roof` all values. No parameter is in the default mask.

At the defaults it has 392 triangles (budget 400); the most is 552 (a gable
over a wall of 20 points).

### Signpost

`prop.signpost` is a signpost at its origin. Parameters `height` (0.8 wall
heights, 1 - 6 m), `arms` (arrow boards, 1 - 4, default 2), `arm_length` (0.6 m,
0.3 - 1.5), `lean` (0 - 6 degrees, default 2) and `seed`; slot `wood`;
connector base; a triangle mesh body; seeded; tags `prop` and `sign`.

- A square post under a point stands on a foot block and leans in a direction
  the seed draws; its point is the height at every lean.
- Each arm is an arrow board, a five-sided prism with its point ahead, against
  the post's side at a heading the seed draws, one under the other from under
  the post's point; it stands off the post's axis by the square post's reach
  across its heading, so it clears the post's corners.
- The parts are the foot block, the post and a board to an arm:
  26 + 16 * `arms` triangles.

Derive ranges: `height` 0.7 - 0.9 wall heights (step 0.05), `arms` 1 - 4,
`arm_length` 0.5 - 0.8 m (0.1), `lean` 0 - 6 degrees (1). `arms` and `lean` are
in the default mask.

At the default seed it has 74 triangles (three derived arms; budget 100); the
most is 90 (four arms).

## Ruin kit

`kit_shapes_extra_01_08.cpp`, `kit_shapes_extra_17_24.cpp` and
`kit_shapes_extra_33_36.cpp`.

### Broken wall

`ruin.broken_wall` is `architecture.wall` in ruins. Parameters `length`,
`height` and `thickness` (the wall's, with its defaults and ranges), `intact`
(the share of the wall's face left standing, 0.3 - 0.8, default 0.55), `break`
(`stepped`, `jagged`, `slope`; default `stepped`), `course` (0.25 m, 0.05 - 1),
`steps` (columns the top falls in, 3 - 24, default 8) and `seed`; slot `wall`;
connectors start, end, base, front and back (the wall's, without top); a
triangle mesh body; seeded; seam sides -X and -Y; tags `wall`, `ruin` and
`tiles`.

- The start (-X) is the wall's whole end face at the full height, so it joins
  an intact wall or a door wall flush; turned half a turn it joins at its other
  end. From there the top stays whole, falls through a break and ends in a low
  stub of about 0.15 of the height at the end (+X). The break lies where the
  face left standing is `intact` of the whole (within about 0.1).
- The top falls in `steps` columns of seeded widths; each column's end stands
  over the next column's start, so the outline is one closed part of
  12 * `steps` triangles at every value.
- `stepped`: level tops on the courses, a seeded course up or down, each lower
  than the one before; where two columns share a course the second is a chip
  lower. `jagged`: each top rises to a tooth of up to a course over the next
  column's start. `slope`: the tops follow the line of the break with a notch
  between two.
- Replace carries the wall's values: an intact wall run becomes a broken one in
  place, and the seed gives each piece its own break.

Derive ranges: `length`, `height` and `thickness` as the wall's, `intact`
0.3 - 0.8 (step 0.05), `break` all values, `course` 0.2 - 0.3 m (0.05), `steps`
6 - 12. `intact` is in the default mask.

At the defaults it has 96 triangles (budget 100); the most is 288 (24 columns).

### Rubble

`ruin.rubble` is a heap of tumbled ashlar blocks about its origin. Parameters
`radius` (of the round footprint, 1 module, 0.3 - 8 m), `height` (the heap's
height in radii, 0.2 - 0.6, default 0.4), `blocks` (4 - 48, default 16),
`block_size` (a block's length, 0.4 m, 0.1 - 1.5; at most 0.7 of the radius)
and `seed`; slot `stone`; no connectors; a convex hull body; seeded; tags
`ruin` and `rubble`.

- A low mound of debris (a faceted dome of eight points around) fills 0.85 of
  the radius. Half of the blocks (at least four) lie flat on the ground around
  its foot, one to a sector, so the base surrounds the origin; the others lie
  on the mound, tilted 9 to 31 degrees about a seeded level axis and half sunk
  into it. A block is a box of seeded proportions, 0.8 - 1.3 of the block size
  long.
- Every point stays within the radius, and the highest point is the height:
  the heap is squeezed or stretched along Y to it.
- The parts are the mound and a box to a block: 46 + 12 * `blocks` triangles.

Derive ranges: `radius` 0.5 - 2 modules (step 0.25), `height` 0.2 - 0.6 (0.05),
`blocks` 8 - 24, `block_size` 0.25 - 0.5 m (0.05). `radius` and `height` are in
the default mask.

At the defaults it has 238 triangles (budget 240); the most is 622 (48 blocks).

### Broken pillar

`ruin.broken_pillar` is `architecture.pillar` in ruins. Parameters `profile`,
`width`, `height` and `segments` (the pillar's, with its defaults and ranges),
`standing` (the stump's height in heights, 0.2 - 0.7, default 0.4), `fallen`
(drums, 0 - 3, default 2) and `seed`; slot `trim`; connector base; a triangle
mesh body; seeded; tags `pillar` and `ruin`.

- The stump stands at the origin, `standing` of the height high at its axis,
  under a break that slants in a seeded direction (a rise of 0.15 - 0.45 for
  each meter across, less on a low stump).
- Each fallen drum lies on a side on the ground in its own sector about the
  stump, 1.3 - 2 widths away, 0.7 - 1.3 widths long, at a seeded heading, one
  end broken on a slant.
- A square pillar's parts are rings of four points, a round one's of
  `segments`: (1 + `fallen`) * (4 * points - 4) triangles.
- Replace carries the pillar's values.

Derive ranges: `profile`, `width` and `height` as the pillar's, `standing`
0.2 - 0.7 (step 0.05), `fallen` 0 - 3. `standing` and `fallen` are in the
default mask.

At the default seed it has 24 triangles (one derived drum; budget 60); the most
is 1008 (three drums of 64 points).

### Broken arch

`ruin.broken_arch` is `architecture.arch` in ruins. Parameters `span`, `rise`,
`depth`, `thickness` and `segments` (the arch's, with its defaults and
ranges), `gap` (the share of the ring that is gone, 0.15 - 0.5, default 0.3),
`gap_center` (where the gap's middle lies along the ring from the foot at +X,
0.25 - 0.75, default 0.5) and `seed`; slot `trim`; connectors start, end (the
arch's, at the feet) and base; a triangle mesh body; tags `arch` and `ruin`.

- Two arms stand on the arch's two feet, each from its foot up to a break cut
  across the ring; the ring between the breaks is gone. An arm is at least
  0.08 of the ring.
- Each arm has `segments` / 2 + 1 segments. An arm that reaches beyond the
  crown has its segments up to the crown and one beyond it, so no segment lies
  across the crown.
- The generator draws nothing from the seed; with `gap` and `gap_center`
  derived, the seed places the gap.
- Replace carries the arch's values.

Derive ranges: `span`, `rise`, `depth` and `thickness` as the arch's, `gap`
0.15 - 0.5 (step 0.05), `gap_center` 0.25 - 0.75 (0.05). `gap` and `gap_center`
are in the default mask.

At the defaults it has 120 triangles (budget 130); the most is 408 (48
segments).

### Cracked floor

`ruin.cracked_floor` is `basic.floor_slab` in ruins. Parameters `width`,
`depth` and `thickness` (the slab's, with its defaults and ranges),
`fragments` (3 - 24, default 9), `tilt` (the most a fragment tilts, 0 - 4
degrees, default 2), `missing` (the share of fragments that are missing,
0 - 0.2, default 0.1) and `seed`; slot `floor`; the slab's six connectors
(base, top, front, back, left, right); a box body with no fragment missing
or tilted and a thickness of at least 4 cm (twice the most a fragment sinks),
a triangle mesh body otherwise; seeded; tags `walkable`, `floor`, `ruin` and
`tiles`.

- Seeded straight cuts split the slab into rows across Z (as many as the root
  of the count, rounded down; the first rows take the fragments left over) and
  each row into its fragments, so every fragment has four corners. A crack of
  up to 1.2 cm runs around each; the slab's four sides stay whole.
- A fragment is a prism from the base to a tilted top: it tilts toward a
  seeded heading by a seeded share of `tilt` (a corner rises at most 0.1 m
  over the fragment's middle) and sinks by up to 2 cm besides, so its highest
  corner is at most the slab's top; on a thin slab it sinks and tilts less
  (at most 0.7 of the thickness in all).
- A missing fragment (each with the chance `missing`) lies level and deep:
  0.85 of the thickness under the slab's top, at most 0.3 m.
- Walkability (R2.10): the check refuses a floor whose greatest step between
  two fragments (a sunk and tilted one, or a missing one) is over the agent's
  climb.
- The parts are the fragments: 12 triangles each.
- Replace carries the slab's values.

Derive ranges: `width`, `depth` and `thickness` as the slab's, `fragments`
6 - 16, `tilt` 0 - 4 degrees (step 0.5), `missing` 0 - 0.2 (0.05). `tilt` and
`missing` are in the default mask.

At the defaults it has 108 triangles (budget 120); the most is 288 (24
fragments).

## Traversal kit

`kit_shapes_extra_01_08.cpp`, `kit_shapes_extra_17_24.cpp` and
`kit_shapes_extra_33_36.cpp`.

### Ladder

`traversal.ladder` is a ladder standing on its origin. Parameters `height`
(1 wall height, 0.5 - 12 m), `width` (0.5 modules, 0.3 - 1.5 m), `rung_spacing`
(the most from a rung to the next, 0.3 m, 0.15 - 0.5; in meters and not
derivable), `lean` (0 - 20 degrees toward -Z, default 10) and `seed`; slot
`wood`; connectors bottom (at the origin, facing down) and top (on the rails'
tops, facing -Z); a triangle mesh body; mirror plane x; tags `traversal`,
`ladder` and `climb`.

- Two rails stand on the ground with level feet and level tops; with a lean
  the tops stand `height` * tan(`lean`) toward -Z of the feet. The rungs are
  level boxes in equal steps from the ground to the top, each step at most the
  rung spacing: the step count is the height over the spacing, rounded up (at
  most 81), and the rungs are one less.
- The bounds are the width and the height; the parts are two rails and the
  rungs, 12 triangles each.
- The reachable area crosses a ladder only with a navigation link, which is
  not built: the walkable reads report its two ends as separate islands.

Derive ranges: `height` 0.75 - 1.25 wall heights (step 0.05), `width`
0.4 - 0.6 modules (0.05), `lean` 0 - 20 degrees (5). No parameter is in the
default mask.

At the defaults it has 132 triangles (budget 140); the most is 972 (12 m with
rungs 0.15 m apart).

### Trestle

`traversal.trestle` is a support under a deck at its origin. Parameters
`height` (the cap beam's top, 1 wall height, 0.5 - 16 m), `width` (the cap
beam's length along Z, 1 module, 0.5 - 8 m), `splay` (0 - 15 degrees, default
8), `braces` (0 - 3, default 2) and `seed`; slot `wood`; connectors base and
top; a triangle mesh body; mirror plane x; tags `traversal`, `trestle` and
`support`.

- Two legs stand across Z on a sill, each leaning outward toward its foot by
  the splay, under a cap beam the width long whose top is the height.
- `braces` boards cross the bays between the sill and the cap, each sloping
  the other way from the one below.
- Every part is a box: 4 + `braces` of them.

Derive ranges: `height` 0.5 - 1.25 wall heights (step 0.25), `width`
1 - 2 modules (0.5), `splay` 0 - 15 degrees (2.5), `braces` 0 - 3. No parameter
is in the default mask.

At the defaults it has 72 triangles (budget 80); the most is 84 (three braces).

### Scaffold

`traversal.scaffold` is one scaffold bay at its origin. Parameters `width`
(along X, 2 modules, 1 - 8 m), `depth` (along Z, 1 module, 1 - 4 m), `levels`
(decks, 1 - 6, default 2), `level_height` (from a deck to the next, 2.4 m,
2.2 - 6; in meters and not derivable), `braces` (`none`, `single`, `cross`;
default `cross`) and `seed`; slots `wood` and `metal`; connectors base, top
(on the top deck), left, right, front and back (at the base's sides); a
triangle mesh body; tags `walkable`, `traversal`, `scaffold` and `tiles`.

- Four poles stand on base plates (slot `metal`) at the bay's corners and
  reach 1 m over the top deck, where a guard rail runs along the back and the
  two sides.
- Every level has a deck of three planks along X, its top at the level's
  height, on two ledgers along X and two transoms along Z.
- `single`: one brace (slot `metal`) crosses the back face (-Z) under every
  deck, the other way at every second level. `cross`: two cross it.
- The bay fills its width and depth: bays join side to side and back to front,
  and stack base on top.
- Walkability (R2.10): the check refuses decks whose clear depth between the
  poles is under the agent's width plus the opening margin, and a level whose
  headroom under the next deck's transoms (the level height less 0.12 m) is
  under the agent's height plus the opening margin.
- The parts are 11 boxes and 7 to a level, and a level's braces.

Derive ranges: `width` 2 - 3 modules (step 0.5), `depth` 1 - 1.5 modules
(0.25), `levels` 1 - 3, `braces` all values. No parameter is in the default
mask.

At the defaults it has 348 triangles (budget 400); the most is 780 (six
cross-braced levels).

### Rope bridge

`traversal.rope_bridge` is a rope bridge along X between its two ends.
Parameters `span` (6 modules, 2 - 16 m), `width` (between the hand ropes, 1 m,
1 - 3; in meters and not derivable), `sag` (how far the deck's middle hangs
under its ends, 0.03 - 0.1 of the span, default 0.06), `plank_gap` (0.03 m,
0.01 - 0.04; not derivable), `missing` (the share of planks that lie loose,
0 - 0.15, default 0.08), `rail_height` (the hand ropes over the deck at the
ends, 1 m, 0.6 - 1.5) and `seed`; slots `wood` and `cloth`; connectors start
and end (the ends of the walking surface, facing -X and +X); a triangle mesh
body; seeded; tags `walkable`, `traversal`, `bridge` and `rope`.

- The deck hangs in a parabola. Its planks lie across two ropes under them, a
  plank to every 0.2 m and gap of the span (4 to 64); each follows the deck's
  slope, a little off its place and a little low by the seed.
- Two hand ropes hang over the deck's edges, `rail_height` over it at the ends
  and 0.78 of that at mid span, on a hanger at every fourth plank, between two
  posts at each end.
- `missing`: each plank but the first two and the last two lies loose with
  that chance, never two in a row: it lies turned on the next plank and leaves
  a gap of a plank and two plank gaps. A loose plank is flat on a curved deck:
  it is raised by the deck's curvature at its corners' reach, so it rests on
  its ends and no corner lies under the walking surface.
- The origin is under mid span at the bridge's lowest point (the deck ropes
  beside the middle, or the posts' feet); a bridge is hung by its two
  connectors.
- Walkability (R2.10): the check refuses a bridge whose clear width between
  the hand ropes is under the agent's width plus the opening margin, whose
  ends rise steeper than the agent's slope (the ends rise by four sags to a
  span: 21.8 degrees at the most sag), or whose widest gap (a loose plank's,
  where `missing` is over 0) is wider than the agent's radius.
- The parts are the planks, four ropes, two hangers at every fourth plank and
  four posts.

Derive ranges: `span` 4 - 10 modules (step 1), `sag` 0.03 - 0.1 (0.01),
`missing` 0 - 0.15 (0.01), `rail_height` 0.9 - 1.2 m (0.1). `sag` and `missing`
are in the default mask.

At the defaults it has 1000 triangles (budget 1010); the most is 1696 (16 m of
the narrowest planks).

## Harbour kit

`kit_shapes_extra_01_08.cpp`, `kit_shapes_extra_09_16.cpp`,
`kit_shapes_extra_17_24.cpp`, `kit_shapes_extra_25_32.cpp` and
`kit_shapes_extra_33_36.cpp`.

### Pier

`harbour.pier` is a timber pier along X. Parameters `length` (2 modules,
1 - 16 m), `width` (2 m, 1.2 - 8; in meters and not derivable, since the agent
fixes the least width), `pile_depth` (the walking surface over the piles'
feet, 1 wall height, 0.5 - 16 m), `pile_spacing` (from a bent to the next,
2 m, 1 - 8), `braces` (`none`, `cross`; default `cross`), `plank_jitter`
(0 - 1, default 0.4), `seed` and `segments` (points around a pile, 6 - 12,
default 8); slot `wood`; connectors start and end (at the ends of the walking
surface, facing -X and +X) and left and right (at its sides, facing -Z and
+Z); a triangle mesh body; seeded; tags `walkable`, `pier`, `harbour`, `floor`
and `tiles`.

- The base is the piles' feet: the piece is sunk into the water by what the
  piles stand in, and its walking surface is `pile_depth` over the base.
- The deck is planks across the pier, one to every 0.25 m of the length (at
  most 64) with a gap of 2 cm between two, on two stringers along X. With
  `plank_jitter` each plank's two ends stand back from the pier's sides by up
  to 0.1 m and its top lies up to 8 mm under the walking surface; at 0 the
  ends are even and the tops are one level.
- A bent stands at the middle of every `length` / `pile_spacing` part of the
  length (rounded, 1 to 8): two round piles, a little thinner at the foot,
  under a cap beam across the whole width. `cross` adds two braces across the
  bent, one on each face of the piles.
- The bounds are the length by the width: two piers joined end to start go on
  with their planks, stringers and bents at an even pitch, and two side by
  side meet at their cap beams.
- Walkability (R2.10): the check refuses a deck narrower than the agent's
  width plus the opening margin, and a gap between planks over a tenth of the
  agent's radius.

Derive ranges: `length` 2 - 6 modules (step 1), `pile_depth` 0.5 - 1.25 wall
heights (0.25), `pile_spacing` 1.5 - 3 m (0.5), `braces` all values,
`plank_jitter` 0.2 - 1 (0.1). `plank_jitter` is in the default mask.

At the defaults it has 212 triangles (budget 220); the most is 1784 (16 m on
eight braced bents of piles of twelve points).

### Mooring

`harbour.mooring` is a mooring fixture at its origin. Parameters `form`
(`bollard`, `cleat`, `post`, `pile_cluster`; default `bollard`), `size` (0.5 m,
0.2 - 2), `lean` (0 - 8 degrees, default 3), `rope` (`none`, `coil`; default
`none`), `seed` and `segments` (points around the round parts, 6 - 16, default
10); slots `metal` and `wood`; connector base; a convex hull body; seeded; tags
`harbour` and `mooring`.

- `bollard`: a cast bollard (slot `metal`) one size high on a square timber
  pad: a flange, a waist and a wide head.
- `cleat`: a horn cleat one size long along X on a timber pad: two legs under
  a six-sided bar that thins to a point at both ends; 0.42 sizes high.
- `post`: a round timber post 2.4 sizes high under an iron band and an iron
  cap.
- `pile_cluster`: three round piles 4 sizes high (two of them up to a tenth
  shorter) standing a third of a turn apart and leaning together, bound by an
  iron hoop that follows the three piles.
- The upright parts lean by `lean` in a direction the seed draws: every point
  moves level by the tangent of the lean for its height, so feet and tops stay
  level and a pad stays flat.
- `coil`: three turns of rope (slot `wood`), each a ring of a square section
  on its corner, a little tilted: around a bollard's waist, a cleat's legs, a
  post's middle, a pile cluster under its hoop.

Derive ranges: `form` all values, `size` 0.4 - 0.6 m (step 0.05), `lean`
1 - 8 degrees (1), `rope` all values. `lean` is in the default mask.

At the defaults it has 188 triangles (budget 200); the most is 680 (a bollard
of 16 points with a rope).

### Quay wall

`harbour.quay_wall` is a quay segment along X, walkable on top. Parameters
`length` (2 modules, 1 - 32 m), `height` (the walking surface, 1 wall height,
0.5 - 16 m), `thickness` (the top's size along Z, 1 module, 1 - 8 m), `batter`
(how far the water face leans from upright, 0 - 10 degrees, default 5),
`fenders` (0 - 4, default 2) and `seed`; slots `stone` and `wood`; connectors
start and end (at the ends of the walking surface, facing -X and +X), base and
top; a box body without fenders and batter, a triangle mesh body otherwise;
mirror plane x; tags `walkable`, `quay`, `harbour`, `wall` and `tiles`.

- The land face (-Z) is upright at half the thickness. The water face (+Z)
  leans out: its foot stands `height` * tan(`batter`) beyond half the
  thickness, so the top is the thickness wide at every batter. The origin is
  under the middle of the top.
- The wall stands on a plinth at the water's foot under a coping, whose top is
  the walking surface. The wall and the coping are one prism along X: the land
  face rises upright to the walking surface, the coping overhangs the water
  face. A timber kerb (slot `wood`) runs along the water's edge
  on the coping, up to 0.12 m high, and `fenders` timbers lean on the water
  face from over the plinth to under the coping, evenly along the length.
- The ends are the length apart, so segments join end to start.
- Walkability (R2.10): the clear width is the thickness less the kerb (at most
  0.15 m); the check refuses a top narrower than the agent's width plus the
  opening margin. The least thickness, 1 m, leaves 0.88 m.
- The parts are the wall with its coping, the plinth, the kerb and one box to
  a fender.

Derive ranges: `length` 2 - 6 modules (step 1), `height` 0.5 - 1.25 wall
heights (0.25), `thickness` 1 - 2 modules (0.5), `batter` 0 - 10 degrees (2.5),
`fenders` 1 - 3. No parameter is in the default mask.

At the defaults it has 76 triangles (budget 80); the most is 100 (four fenders).

### Buoy

`harbour.buoy` is a buoy at its origin, standing on its foot. Parameters `form`
(`can`, `cone`, `sphere`, `spar`; default `can`), `size` (1 m, 0.4 - 3), `tilt`
(0 - 12 degrees, default 4), `bands` (bands of trim, 1 - 3, default 1), `seed`
and `segments` (points around the float, 8 - 16, default 10); slots `metal`,
`trim` and `glow`; connector base; a convex hull body; seeded; tags `harbour`,
`buoy` and `light`.

- The float is twelve rings of facets: `can` a drum one size high, `cone` a
  drum under a cone 1.25 sizes high, `sphere` a ball one size wide, `spar` a
  pole 2.6 sizes high with a float collar. It is 2 * `bands` + 1 stripes, metal
  and trim in turn.
- A mast with a cross piece stands on the float under a lamp (slot `glow`),
  0.43 sizes over the float.
- The whole leans by `tilt` in a direction the seed draws; its foot stays
  level.
- Light: one point frame `light` with the kit's starting values, 0.5 sizes
  over the float, over the lamp, leaning with the buoy.
- The base is the float's foot: water is no snap to surface target, so the
  buoy is sunk by hand (`doc/editor/level_tools.md` "Surface placement").

Derive ranges: `form` all values, `size` 0.8 - 1.2 m (step 0.1), `tilt`
1 - 12 degrees (1), `bands` 1 - 3. `tilt` is in the default mask.

At the defaults it has 312 triangles (budget 320); the most is 468 (16 points).

### Cargo

`harbour.cargo` is a pallet with a stack of goods at its origin. Parameters
`load` (`crates`, `sacks`, `barrels`, `mixed`; default `crates`), `layers`
(1 - 3, default 2), `fill` (the share of the pallet the goods cover, 0.5 - 1,
default 0.8), `width` (the pallet's size along X and Z, 1 module, 0.6 - 3 m)
and `seed`; slots `wood` and `cloth`; connectors base and top (on the load); a
convex hull body; seeded; tags `harbour`, `cargo` and `stack`.

- The pallet is three runners along X under five boards along Z, 0.14 widths
  high (0.08 - 0.16 m).
- Every layer is four goods about the middle, two by two; `fill` sets their
  size, and the seed moves (up to 0.02 widths) and turns (up to 0.1 rad) each
  a little. Each good's center stands off the middle by its footprint's reach
  at the most turn and move, so the four never meet and stay on the pallet. A crate is a box under a
  lid, 0.36 widths high; a sack (slot `cloth`) lies flat, longer than wide,
  0.15 widths high; a barrel is 0.4 widths high. `mixed` is a layer of crates
  under a layer of sacks under a layer of barrels.
- A folded tarp (slot `cloth`) lies on the load at a seeded turn, so every
  load has both slots.
- The parts are the pallet's eight boxes, the tarp, and four to a layer (eight
  to a layer of crates).

Derive ranges: `load` all values, `layers` 1 - 3, `fill` 0.5 - 1 (step 0.05),
`width` 1 - 1.25 modules (0.25). `layers` and `fill` are in the default mask.

At the default seed it has 300 triangles (budget 320); the most is 828 (three
layers of barrels).

### Crane

`harbour.crane` is a quay crane at its origin. Parameters `form` (`derrick`,
`tower`; default `derrick`), `height` (the mast or the tower's head, 1.5 wall
heights, 2 - 40 m), `jib_length` (4 m, 1 - 20), `jib_heading` (where the jib
points, from +X toward -Z, 0 - 360 degrees, default 0), `jib_angle` (how far
the jib rises from level, 10 - 60 degrees, default 30) and `seed`; slots
`metal` and `wood`; connector base; a triangle mesh body; tags `harbour` and
`crane`.

- `derrick`: a timber mast on two crossed sills under an iron cap, with a
  timber jib from before its foot, held by a line from the mast's head to the
  jib's end.
- `tower`: a lattice tower of four chords and four bays (a rail around each
  bay's top and a diagonal across each face, the other way in the next bay) up
  to 0.8 of the height, under a slewing deck with a head up to the height, a
  jib from the deck's edge, a counter jib of 0.35 of the jib's length with its
  weight, two pendants from the head, and a timber cab beside the jib's foot.
- A hook hangs on a line from the jib's end, half way to the ground. A tower
  refuses a jib whose end does not reach beyond the slewing deck's corners
  (a short jib at a steep angle on a tall tower), since its hook line would
  pass through the deck.
- The generator draws nothing from the seed; with `jib_heading` derived, the
  seed turns the jib.
- The parts are 8 (derrick) and 46 (tower), each a box.

Derive ranges: `form` all values, `height` 1.5 - 2.5 wall heights (step 0.25),
`jib_length` 3 - 6 m (0.5), `jib_heading` 0 - 345 degrees (15), `jib_angle`
10 - 60 degrees (5). `jib_heading` is in the default mask.

At the defaults it has 96 triangles (budget 110); the most is 552 (a tower).

### Lighthouse

`harbour.lighthouse` is a lighthouse at its origin. Parameters `height`
(4 wall heights, 6 - 60 m), `base_radius` (2 modules, 1 - 10 m), `taper` (the
radius under the gallery in base radii, 0.5 - 0.9, default 0.7), `bands` (bands
of trim, 1 - 6, default 3), `gallery` (`none`, `rail`; default `rail`), `seed`
and `segments` (points around the tower, 8 - 24, default 12); slots `wall`,
`trim` and `glow`; connectors base and top (at the roof's point); a triangle
mesh body; mirror plane z; tags `landmark`, `harbour`, `lighthouse` and
`light`.

- The tower tapers from the base radius to the radius under the gallery at
  0.76 of the height, in 2 * `bands` + 1 stripes: wall, and a band of trim
  between each two. A door (slot `trim`) stands out of its foot toward +X.
- The gallery is a deck 1.35 of the upper radius wide; `rail` adds a post at
  every point under a rail 0.7 - 1.2 m high.
- The lantern room (slot `glow`) is a drum 0.62 of the upper radius wide up to
  0.9 of the height, under a roof to a point at the height.
- Light: one spot frame `light` beside the lantern room toward +X, at the
  middle of its glass, shining level toward +X: 4000 cd, a range of 100 m,
  cone angles of 0.15 and 0.3 rad.
- The parts are 5, and a post to every point and the rail with a rail.

Derive ranges: `height` 3 - 5 wall heights (step 0.5), `base_radius`
1.5 - 2.5 modules (0.25), `taper` 0.5 - 0.9 (0.05), `bands` 1 - 6, `gallery`
all values. No parameter is in the default mask.

At the defaults it has 622 triangles (budget 660); the most is 1534 (a rail,
six bands and 24 points).

### Boat

`harbour.boat` is an open boat along X, its bow toward +X. Parameters `form`
(`rowboat`, `skiff`, `barge`; default `rowboat`), `length` (4 m, 2 - 12),
`beam` (the width amidships, 1.4 m, 0.8 - 4), `sheer` (how far the gunwale
rises toward the ends, 0 - 0.3, default 0.15), `thwarts` (0 - 4, default 2),
`heel` (how far the boat lies over toward +Z, 0 - 25 degrees, default 0) and
`seed`; slots `wood` and `trim`; connector base; a triangle mesh body; tags
`harbour` and `boat`.

- The hull is one part of two skins: eleven stations along the keel, nine
  points across each from gunwale over keel to gunwale, the outer skin and the
  inner skin a skin's thickness inside it, joined by a rim along both gunwales
  and across both ends. Its facets are triangles. The rim and the top strake
  of both sides are slot `trim`.
- `rowboat`: a round section, a transom at the stern and a stem at the bow,
  where the two sides share their points, the keel rising toward the bow.
  `skiff`: a flat bottom out to a chine under flared sides, closing to a stem
  the same way. `barge`: a flat bottom under nearly upright sides, the
  ends square and raked. The hull is 0.36 beams deep (a barge 0.3).
- `sheer` lifts the gunwale toward the ends by up to 0.25 of it times the
  length, the bow more than the stern.
- A board closes each end inside the hull, `thwarts` boards cross it at 0.62
  of its depth and reach into the inner skin at their lower face, and two
  sleepers lie level on the ground under it at stations 3 and 7, each up to
  the hull's lowest point over its station, so the hull rests on both.
- `heel` turns the hull, its boards and its thwarts about the keel line toward
  +Z; the hull's lowest point lies on the sleepers at every heel, so the base
  is level: water is no snap to surface target, and the boat is sunk by hand
  (`doc/editor/level_tools.md` "Surface placement").
- The generator draws nothing from the seed; with `length` and `heel`
  derived, the seed sizes and heels the boat.

Derive ranges: `form` all values, `length` 3 - 8 m (step 0.5), `beam`
1.2 - 1.8 m (0.1), `sheer` 0 - 0.3 (0.05), `thwarts` 0 - 4, `heel` 0 - 25
degrees (5). `length` and `heel` are in the default mask.

At the default seed it has 488 triangles (budget 520); the most is 512 (a
rowboat with four thwarts).

## Light kit

`kit_shapes_extra_01_08.cpp`, `kit_shapes_extra_17_24.cpp` and
`kit_shapes_extra_33_36.cpp`.

### Lamp post

`light.lamp_post` is a lamp post at its origin with a light to a head
(`doc/editor/level_tools.md` "Lights on pieces"). Parameters `height` (1.2 wall heights, 1.5 - 12 m),
`arms` (0 - 4, default 0), `arm_length` (from the post's axis to a head on an
arm, 0.6 m, 0.3 - 2), `head` (`lantern`, `globe`, `hood`; default `lantern`),
`seed` and `segments` (points around the post, 6 - 16, default 8); slots
`metal` and `glow`; connector base; a convex hull body; mirror plane z; tags
`light` and `lamp`.

- The post is one part: a plinth, a pedestal, a tapering shaft and a collar.
  Its sizes follow a unit of 0.028 of the height (0.045 - 0.16 m), the shaft's
  radius at its foot; the heads are sized in it, but a head on an arm too
  short for it is sized smaller, so it keeps clear of the post's collar
  (1.2 units): the head's unit is at most the arm's length less the collar,
  over the head's reach toward the post (a lantern 1.65 units, a globe 1.6, a
  hanging hood 2.5).
- `arms` 0: one head stands on the post, and its top is the height. 1 to 4: the
  post ends under a finial whose point is the height, and each arm is a beam
  from the post with a strut under it, the first toward +X, the others at
  equal angles around the post; a head stands on each arm's end.
- `lantern`: a four-sided lantern turned with its arm: a neck and a plate, a
  glowing core (slot `glow`) between four bars, eaves and a roof to a point.
- `globe`: a glowing globe in a cup.
- `hood`: on the post, a glowing bulb on a plate under a wide hat on four
  rods; on an arm, a shade hanging from the arm's end over a glowing disc.
- Lights: one frame to a head, named `light_1` to `light_4` by the head's
  number (`light_1` is the head on the post, or on the arm toward +X), with
  the starting values of a kit light (a warm white point light of 40 cd with a
  range of 8 m); under a hood it starts as a spot shining down with cone
  angles of 1.2 and 2 rad. The frame stands in the open by its head's glowing
  part: over a lantern's core under the eaves, over a bulb under the hat,
  0.3 units over a globe's top, and 0.3 units under a hanging hood's disc.

Derive ranges: `height` 1 - 1.4 wall heights (step 0.1), `arms` 0 - 4,
`arm_length` 0.5 - 0.9 m (0.1), `head` all values. No parameter is in the
default mask.

At the defaults it has 258 triangles (budget 270); the most is 1322 (four
globes on a post of 16 points).

### Lantern

`light.lantern` is a cage lantern at its origin. Parameters `mount`
(`standing`, `hanging`, `bracket`; default `standing`), `size` (the lantern's
width, 0.3 m, 0.15 - 1.5), `chain_length` (0.5 m, 0.1 - 4), `sides` (4 - 8,
default 6) and `seed`; slots `metal` and `glow`; connectors base, top and
back; a convex hull body; tags `light` and `lantern`.

- The body is 1.7 sizes high: a plate, a glowing core (slot `glow`) inside a
  bar to each of the `sides` corners, eaves, a roof to a point and a loop.
- `standing`: it stands on its plate. `hanging`: a chain of five links, each
  turned a quarter against the one below, rises `chain_length` from the loop;
  the top connector is at the chain's top. `bracket`: a link leads up to an arm
  from a plate on the wall behind the lantern (-Z), 1.3 sizes from its axis,
  over a strut; the back connector is on that wall at the arm's height.
- The base is the plate's foot at every mount: a hanging lantern and one on a
  bracket attach by top and back.
- Light: one point frame `light` with the kit's starting values, on the axis
  0.93 sizes up: over the core, under the eaves, inside the cage.
- The parts are 5 + `sides`, five more with a chain, four more with a bracket.

Derive ranges: `mount` all values, `size` 0.25 - 0.4 m (step 0.05),
`chain_length` 0.3 - 1 m (0.1), `sides` 4 - 8. No parameter is in the default
mask.

At the defaults it has 190 triangles (budget 200); the most is 314 (a hanging
lantern of eight sides).

### Fire

`light.fire` is an open flame at its origin. Parameters `form` (`brazier`,
`torch`, `campfire`; default `brazier`), `size` (0.6 m, 0.3 - 2), `flame` (the
flame's height, 0.4 m, 0.1 - 1.5) and `seed`; slots `metal`, `glow` and
`stone`; connectors base and back; a convex hull body; seeded; tags `light`
and `fire`.

- `brazier`: a bowl one size high and wide on three legs, on a round plinth of
  slot `stone`, with embers (slot `glow`) in the bowl.
- `torch`: a shaft standing on the origin and leaning toward +Z, through a ring
  on an arm from a stone on the wall behind it (0.12 sizes toward -Z), under a
  cup; the back connector is on that wall.
- `campfire`: embers in a ring of seven stones 1.5 sizes wide, under a tripod
  of three rods (slot `metal`) that cross on the axis over the flame, at 0.85
  of their height.
- The flame (slot `glow`) is rings of six points up to a point, `flame` high
  over its foot; the seed sets each point's reach and where the tip stands.
  The campfire's stones take their sizes and places from the seed too.
- Light: one point frame `light` 0.06 m over the flame's tip, with the color
  (1, 0.62, 0.3) and the kit's other starting values.
- The parts are 7 (brazier), 5 (torch) and 12 (campfire).

Derive ranges: `form` all values, `size` 0.5 - 0.8 m (step 0.1), `flame`
0.3 - 0.6 m (0.05). `flame` is in the default mask.

At the defaults it has 230 triangles (budget 240), which is also the most.

### Garland

`light.garland` is a garland along X between its two ends. Parameters `span`
(4 modules, 1 - 24 m), `sag` (how far the wire's middle hangs under its ends,
0.05 - 0.15 of the span, default 0.1), `count` (3 - 24, default 8), `item`
(`bulb`, `flag`, `paper_lantern`; default `bulb`) and `seed`; slots `metal`,
`glow` and `cloth`; connectors start and end (the wire's ends, facing -X and
+X); a triangle mesh body; mirror plane x; tags `light` and `garland`.

- The wire (slot `metal`) hangs in a parabola, a stretch between each two
  points; `count` items hang at even steps along it.
- `bulb`: a bulb (slot `glow`) in a socket. `flag`: a pennant (slot `cloth`),
  its upper edge along the wire and its point down; a lamp hangs on a cord at
  mid span under the flags, so a garland of flags has glow. `paper_lantern`: a
  lantern (slot `glow`) on a cord under a cap.
- Two ends of a ribbon (slot `cloth`) hang from each end of the wire, so every
  item has cloth.
- The origin is under mid span at the garland's lowest point (the lowest item,
  or the lamp under the flags); a garland is hung by its two connectors.
- Light: one point frame `light` with the kit's starting values at mid span,
  0.22 m toward +Z of the wire, beside what hangs there.
- The parts are the wire, four ribbon ends, and two to a bulb, one to a flag
  (and three of the lamp), three to a paper lantern.

Derive ranges: `span` 3 - 6 modules (step 0.5), `sag` 0.05 - 0.15 (0.01),
`count` 6 - 12, `item` all values. `sag` is in the default mask.

At the defaults it has 492 triangles (budget 500); the most is 1884 (24 paper
lanterns).

## Industrial kit

`kit_shapes_extra_09_16.cpp` and `kit_shapes_extra_25_32.cpp`.

### Pipe

`industrial.pipe` is a pipe piece lying on the ground from its start at -X.
Parameters `form` (`straight`, `elbow`, `tee`; default `straight`), `diameter`
(0.4 m, 0.1 - 0.5), `length` (2 modules, 1 - 16 m), `bend_angle` (how far an
elbow turns, 15 - 90 degrees, default 90), `flanges` (`none`, `ends`; default
`ends`), `seed` and `segments` (points around the pipe, 6 - 16, default 10);
slot `metal`; connectors start, end and branch; a triangle mesh body; tags
`industrial` and `pipe`.

- A tube of rings of `segments` points, a side of each ring down, with open
  ends: a bore 0.8 of the radius wide reaches 0.9 radii into each end.
- `straight`: along X, the length long. `elbow`: two legs of half the length
  meet at the origin; the pipe turns toward -Z by the bend angle about an
  upright axis, in six bands, its bend 1.5 diameters in radius (less where the
  legs are short, never under 0.75 diameters). `tee`: a straight run with a
  branch from its middle toward -Z, half the length long, a second closed
  part.
- `ends`: a flange 1.35 radii in radius stands at every end, and the pipe rests
  on its flanges; a flanged tee has a collar around its branch's root. `none`:
  the pipe lies on its side.
- Connectors: start at the start's mouth facing -X; end at the end's mouth
  facing along the last leg, so pieces chain end to start; branch at a tee's
  branch mouth facing -Z, on a straight pipe's side facing -Z, and on the
  outside of an elbow's bend.

Derive ranges: `form` all values, `diameter` 0.3 - 0.5 m (step 0.1), `length`
2 - 4 modules (1), `bend_angle` 45 or 90 degrees, `flanges` all values. No
parameter is in the default mask.

At the defaults it has 196 triangles (budget 210); the most is 568 (a flanged
tee of 16 points).

### Tank

`industrial.tank` is a tank at its origin. Parameters `radius` (1 module,
0.3 - 8 m), `height` (1 wall height, 1 - 32 m), `pose` (`upright`, `lying`;
default `upright`), `ends` (`flat`, `dome`, `cone`; default `dome`), `bands`
(0 - 4, default 2), `seed` and `segments` (points around the drum, 8 - 24,
default 12); slots `metal` and `trim`; connectors base and top; a convex hull
body; mirror plane z; tags `industrial` and `tank`.

- One closed drum with `bands` bands (slot `trim`) standing out of it, evenly
  along it. Its ends are flat, domed (0.35 radii deep) or pointed (0.55 radii
  deep), at most a third of the tank each.
- `upright`: it stands on four legs (slot `trim`) and is the height high with
  them. `lying`: it lies along X, the height long, on two saddles (slot
  `trim`), a side of its rings down; the top connector is on its upper side.
- The legs or the saddles are trim at every band count.

Derive ranges: `radius` 0.75 - 1.5 modules (step 0.25), `height`
0.75 - 1.5 wall heights (0.25), `pose` and `ends` all values, `bands` 0 - 4. No
parameter is in the default mask.

At the defaults it has 380 triangles (budget 400); the most is 1100 (an upright
domed tank of 24 points with four bands).

### Truss

`industrial.truss` is a lattice beam along X. Parameters `length` (4 modules,
1 - 32 m), `width` (0.5 modules, 0.2 - 4 m), `height` (0.5 modules, 0.2 - 4 m),
`bays` (2 - 16, default 6), `pattern` (`warren`, `pratt`, `cross`; default
`warren`) and `seed`; slot `metal`; connectors start and end (at the base's
ends, facing -X and +X), base and top; a triangle mesh body; mirror plane z;
tags `industrial`, `truss`, `beam` and `tiles`.

- Four chords run the length at the corners of the width by the height, tied
  at every node by a strut across over the bottom chords and one under the top
  chords.
- The lattice lies in the two faces toward +Z and -Z. `warren`: a diagonal to
  a bay, each the other way from the last. `pratt`: a post at every node and
  diagonals that fall toward the middle. `cross`: two diagonals to a bay.
- The truss fills its length, width and height; trusses chain end to start,
  and one turned upright is a mast.
- Every part is a box: 4 chords, 2 struts to a node, a Pratt truss's 2 posts
  to a node, and 2 diagonals to a bay (4 crossed).

Derive ranges: `length` 3 - 8 modules (step 1), `width` and `height`
0.5 - 1 modules (0.25), `bays` 4 - 8, `pattern` all values. No parameter is in
the default mask.

At the defaults it has 360 triangles (budget 370); the most is 1248 (a Pratt
truss of 16 bays).

### Container

`industrial.container` is a shipping container along X, 2.4 m wide and 2.6 m
high. Parameters `length` (`short`, `standard`, `long`: 3, 6 or 12 m; default
`standard`), `ribs` (ribs of a side to every 3 m, 2 - 6, default 4), `door`
(`closed`, `ajar`, `open`; default `closed`), `door_angle` (5 - 60 degrees,
default 25) and `seed`; slots `metal` and `trim`; connectors base, top, front
and back (the long sides, facing +Z and -Z); a box body with the doors
closed, a triangle mesh body with a door ajar or open; tags
`industrial`, `container` and `stack`.

- A frame (slot `trim`) of four posts and eight rails holds two sides, a roof,
  a floor and a back; the container is hollow. Ribs stand out of both sides
  between the posts.
- Two door leaves hang on the posts at the +X end, each under two locking bars
  (slot `trim`). `closed`: both shut. `ajar`: the leaf at +Z stands open by
  the door angle. `open`: both stand open by a quarter turn and the door
  angle.
- Containers stack base on top and stand side by side by front and back.
- Every part is a box: 23 and the ribs.

Derive ranges: `length` all values, `ribs` 2 - 6, `door` all values,
`door_angle` 5 - 60 degrees (step 5). `door_angle` is in the default mask.

At the defaults it has 468 triangles (budget 480); the most is 852 (12 m with
six ribs to every 3 m).

### Vent

`industrial.vent` is a rooftop unit at its origin, within its width and
height. Parameters `form` (`louvre_box`, `fan`, `stack`, `duct_elbow`; default
`louvre_box`), `width` (1 module, 0.3 - 4 m), `height` (1 module, 0.3 - 4 m),
`slats` (3 - 8, default 5) and `seed`; slot `metal`; connector base; a box
body for the louvre box, a convex hull body for the other forms; tags
`industrial`, `vent` and `roof`.

- `louvre_box`: a box on a curb under a cap, with `slats` slats on its faces
  toward +Z and -Z, leaning out and down.
- `fan`: a drum on a curb with `slats` blades across its mouth around a hub,
  whose point is the height.
- `stack`: a pipe on a flange under a rain cap the width wide, on `slats`
  struts around the pipe's mouth.
- `duct_elbow`: a duct up from a curb at the back, turning toward +Z to a
  mouth of `slats` slats; the riser and the arm are one prism of an L
  section.
- The parts are three (two of a duct elbow) and the slats (on two faces of a
  louvre box).

Derive ranges: `form` all values, `width` and `height` 0.5 - 1.5 modules (step
0.25), `slats` 3 - 8. `width` and `height` are in the default mask.

At the default seed it has 156 triangles (budget 170); the most is 228 (a
louvre box of eight slats).

### Antenna

`industrial.antenna` is an antenna mast on a plate at its origin. Parameters
`form` (`dish`, `whip`, `array`; default `dish`), `height` (1 wall height,
1 - 20 m), `elements` (1 - 5, default 2), `tilt` (0 - 40 degrees, default 15)
and `seed`; slot `metal`; connector base; a triangle mesh body; seeded; tags
`industrial`, `antenna` and `roof`.

- `dish`: the mast reaches the height; each element is a dish with a feed on
  the mast's side, one under the other, at a seeded heading and of a seeded
  size, its axis tilted up by `tilt`. With more than one dish a dish's radius
  is at most what keeps it clear of the next one 0.15 heights down.
- `whip`: the mast reaches 0.55 of the height; each element is a rod from the
  mast's head, leaning out by `tilt` at a seeded heading; the first reaches
  the height.
- `array`: the mast reaches the height; each element is a bar across the
  mast, one under the other, level along a seeded heading and rolled by
  `tilt`.
- The parts are the plate, the mast and the elements (a dish is two).

Derive ranges: `form` all values, `height` 0.75 - 1.5 wall heights (step
0.25), `elements` 1 - 5, `tilt` 0 - 40 degrees (5). `elements` and `tilt` are
in the default mask.

At the default seed it has 208 triangles (budget 220); the most is 472 (five
dishes).

## Machine kit

`kit_shapes_machine_01_08.cpp` and `kit_shapes_machine_09_18.cpp`: gears,
fasteners, the crank train, bearings, a spring and the cam train.

- Every length is in meters (`Kit_default_scale::meters`); none follows the
  level's module or wall height. The defaults are oversized decorative parts
  that read at level scale (a gear about half a meter across).
- A part lies or stands on the ground as a prop does: a gear, a cam, a
  bearing, a pulley, a washer and a nut lie flat with their axis along Y; a
  bolt stands on its tip, a piston on its skirt; the shafts (crankshaft,
  camshaft, worm) lie along X on stands or on their thread.
- No generator draws from the seed and no parameter is in the default mask;
  every parameter but the seed and the two angles (`cone`, `step`) is
  derivable. All shapes are faceted.
- Slots: `metal` (cast iron, Kit Metal) and `steel` (machined surfaces, Kit
  Steel).
- Construction: a lathe is rings about an axis, each band planar quads
  (`add_lathe()`); a gear tooth is a prism of a six-point outline standing on
  the pitch circle, an involute's stand-in: 2.1 modules wide a little inside
  the root circle, half a pitch wide on the pitch circle and 0.8 modules wide
  at its tip, one module beyond the pitch circle (`add_gear_tooth()`; a
  worm wheel's teeth lean, a helical gear's); a bored prism is a prism of m
  sides with a round bore of 2m points (`add_bored_prism()`); a wire is a
  tube of four corners along a path, in triangles, between two end faces
  (`add_wire()`; the corners along its two cross directions may stand off
  differently): a spring's wire, a worm's thread, a lock washer.
- Connectors: `base` is at the origin facing -Y. `axis` is on a shaft or bore
  axis, at the part's top for a part lying flat. `mesh` is a point on a pitch
  circle or pitch line where teeth engage: every `mesh` connector has a tooth
  centered a quarter pitch from it, so two parts of one `module` joined mesh
  to mesh have their teeth half a pitch apart and engage, whatever their
  tooth counts. A gear, a bevel gear and a worm wheel carry four such
  boundary connectors: `mesh` on +X and `mesh_2`, `mesh_3` and `mesh_4` at
  the teeth at or before one, two and three quarter turns from it toward -Z
  (the tooth count times the quarter turns over four, rounded down, teeth
  from +X), each with a tooth a quarter pitch before it as before `mesh`
  (U53). Two gears of one module joined by any two of them engage with
  their axes parallel and the sum of the pitch radii apart; two bevel gears
  joined by them engage as by `mesh`, their axes at the sum of the cone
  angles; a worm wheel's take its worm at any of the four places. Across
  kinds (a gear and a bevel gear, a gear or another wheel and a worm
  wheel's leaning teeth) the connectors join without engaging.

The parts that mate:

- Gear trains: two gears, a gear and a rack, a bevel gear pair and a worm
  and its wheel engage when their `module` is equal. The pitch is pi times
  the module; a gear's, a bevel gear's (at its large end) and a worm
  wheel's pitch diameter is its teeth times the module (16 teeth at 0.03 m:
  0.48 m); a worm's lead is pi times the module for each start.
- Fasteners: a bolt, a nut and a washer of one `diameter` fit: the nut's
  bore is the thread's minor diameter (0.84 of the diameter) and the
  washer's bore 1.1 diameters. A nut joined by its `bolt` connector to a
  bolt's `nut` connector stands around the shank, its bored body between the
  bolt's tip and the connector (one diameter up the shank): upright, or a cap
  nut upside down with its dome over the tip; the bolt's `seat` is the
  surface the head is tightened against.
- Crank train: a piston's `pin` joins a connecting rod's `small_end`, the
  rod's `big_end` a crankshaft's `pin`. A bore's flats are its diameter
  across: at the defaults the rod's small bore (0.05 m) takes the piston's
  wrist pin (0.048 m across its corners) and its big bore (0.08 m) the crank
  pin (the `journal`, 0.08 m across its corners). A cylinder of the piston's
  diameter as its `bore` takes the piston, its rings inside the liner's
  flats.
- Cam train: a cam's `follower` connector joins a follower's `contact`; the
  follower's stem then points away from the cam and a roller's axis lies
  along the cam's. A camshaft's lobes are pear cams.

### Gear

`machine.gear` is a spur gear lying flat: teeth around a rim on a solid
body, a web or spokes, with a bored hub that stands over the face.
Parameters `teeth` (16, 8 - 40; derived 10 - 24), `module` (0.03 m, 0.005 -
0.2; derived 0.02 - 0.04, step 0.01), `face_width` (the teeth's thickness
along Y, 0.08 m, 0.01 - 1; derived 0.06 - 0.12, step 0.02), `bore` (0.08 m,
0.01 - 1; derived 0.06 - 0.1, step 0.02), `body` (`solid`, `web`, `spokes`;
default `web`), `spokes` (5, 3 - 8; derived 4 - 6) and `seed`; slot `metal`;
connectors base, axis (the hub's top), mesh (on the pitch circle at +X,
half the face up, facing +X) and mesh_2 - mesh_4 (the boundary connectors,
on the pitch circle half the face up, facing out); a triangle mesh body;
tags `decoration`,
`obstacle`, `machine` and `gear`.

- The pitch radius is half the teeth times the module, the root circle 1.25
  modules inside it. The bore's radius is at most 0.3 of the root's, the hub
  1.6 bores (at least 0.4 modules more, at most 0.55 of the root) and the
  rim's inside 1.2 modules inside the root circle (at least 0.7 of it). The
  hub stands a quarter of the face over it.
- `solid`: one lathe from the bore to the root circle. `web`: a web a third
  of the face thick between the hub and the rim. `spokes`: the rim and the
  hub as two lathes and a box to a spoke between them, half the face thick.
- The body has two points to a tooth (16 - 48) around; tooth i stands at
  (i - 0.25) of a tooth's angle from +X.

Budget 1100 triangles.

### Gear rack

`machine.rack` is a bar along X lying flat, with teeth toward +Z from the
root line on z = 0, the teeth times the pitch long. Parameters `teeth` (12, 4
- 40; derived 8 - 20), `module`, `face_width` (both as the gear's), `height`
(the bar behind the root line along Z, 0.08 m, 0.01 - 1; derived 0.06 -
0.12, step 0.02) and `seed`; slot `metal`; connectors start and end (the
bar's ends on the root line, facing -X and +X), base and mesh (on the pitch
line 1.25 modules from the root line, a quarter pitch beside the middle,
facing +Z); a triangle mesh body; origin rule `base_contains_origin`;
mirror plane x; tags `decoration`, `obstacle`, `machine`, `gear` and `rack`.
Racks chain end to start with their teeth in step. Budget 260 triangles.

### Bevel gear

`machine.bevel_gear` is a bevel gear lying flat: teeth on a cone that
narrows upward. Parameters `teeth` (16, 8 - 32; derived 10 - 24), `module`
(at the large end, as the gear's), `face_width` (the teeth's length along the
cone, as the gear's), `cone` (degrees between the axis and the pitch cone,
45, 30 - 60; fixed), `bore` (0.08 m, at most half the small end; derived
0.06 - 0.1, step 0.02) and `seed`; slot `metal`; connectors base, axis (the
top), mesh (on the large end's pitch circle at +X, outward across the
cone, up along it toward the apex) and mesh_2 - mesh_4 (the boundary
connectors: mesh turned about Y); a triangle mesh body; tags `decoration`,
`obstacle`, `machine`, `gear` and `bevel`.

- The large end's pitch circle stands 0.6 modules, a tenth of the face width
  and 1.1 modules times the sine of the cone over the ground. The body is a
  lathe up the root cone, 1.25 modules inside the pitch cone at the large
  end and through its apex, to the small end, whose size is 1 less the face
  width times the sine of the cone over the pitch radius (0.4 - 0.95 of the
  large end); the axis connector is on the body's top.
- A tooth is a trapezoid across the cone at the large end (2.1 modules wide
  at its foot 1.4 modules inside the pitch cone, 0.8 at its tip one module
  out) scaled toward the cone's apex to the small end.
- Two gears of 45 degrees joined by their mesh connectors turn shafts at a
  right angle; each one's tooth tips stay a quarter module out of the other's
  body.

Budget 520 triangles.

### Worm

`machine.worm` is a worm, or the worm wheel it drives. Parameters `form`
(`worm`, `wheel`), `module` (as the gear's), `diameter` (the worm's pitch
diameter, 0.12 m, 0.02 - 1, at least three modules; derived 0.1 - 0.16,
step 0.02), `starts` (threads, 1 - 3; derived 1 - 2), `turns` (of each
thread, 4, 2 - 6; derived 3 - 5), `teeth` (of the wheel, 24, 16 - 40; derived
20 - 30) and `seed`; slots `metal` and `steel`; a triangle mesh body; origin
rule `base_contains_origin`; tags `decoration`, `machine`, `gear` and `worm`.

- `worm`: a shaft (slot `steel`) along X under `starts` helical threads (slot
  `metal`), each a wire twelve stretches to a turn whose section is a module
  wide along the axis and reaches a module out of the pitch cylinder and
  1.25 modules into it, as a rack's tooth does. The shaft's corners are 1.15
  modules inside the pitch cylinder (on the thread's inner chords), under a
  joined wheel's tooth tips. The threaded part is the lead times the turns
  long, shifted along X by less than half an axial pitch so that the thread
  crosses the top a quarter pitch beside a gap's middle; the shaft reaches
  two shaft radii and that shift beyond it at both ends. No point of a turn
  lies straight under the axis, so the worm rests on two lines of its
  thread. Connectors base, axis (the shaft's end at +X, facing +X) and mesh
  (on the pitch cylinder over the middle, facing up, up along Z).
- `wheel`: lying flat, a ring from a bore of 0.15 of the root circle to the
  root circle, 1.2 of the worm's radius thick, with teeth and a bush (slot
  `steel`) in its bore that stands a quarter of the face over it. A tooth
  leans along the pitch circle by the worm's lead over the worm's pitch
  circumference for each unit up, centered on its spur position halfway up
  the face. Connectors base, axis (the bush's top), mesh (on the pitch
  circle at +X, facing +X) and mesh_2 - mesh_4 (the boundary connectors).
- A worm joined to its wheel mesh to mesh at twist 0 lies with its axis
  across the wheel's, along the wheel's tangent; a wheel tooth stands in a
  gap of the thread, and the teeth's tips clear the shaft.

Budget 800 triangles.

### Pulley

`machine.pulley` is a belt pulley with V grooves, or a chain sprocket, lying
flat. Parameters `form` (`pulley`, `sprocket`), `diameter` (a pulley's
outside, a sprocket's pitch diameter; 0.3 m, 0.08 - 4; derived 0.2 - 0.4,
step 0.05), `width` (along Y, 0.06 m, 0.02 - 1; derived 0.04 - 0.1, step
0.02), `grooves` (of a pulley, 1, 1 - 4; derived 1 - 3), `teeth` (of a
sprocket, 16, 8 - 32; derived 12 - 24), `bore` (0.05 m, 0.01 - 1; derived
0.04 - 0.08, step 0.01) and `seed`; slot `metal`; connectors base and axis
(the top); a triangle mesh body; tags `decoration`, `machine`, `pulley` and
`sprocket`.

- `pulley`: one lathe of 24 points, the width cut into `grooves` cells, each
  with a V groove 0.35 of the cell deep (at most 0.4 of the rim's depth).
- `sprocket`: a toothed plate 0.3 of the width thick under a hub up to the
  width; its module is the diameter over the teeth, and its teeth are the
  gear's.

Budget 700 triangles.

### Bolt

`machine.bolt` is a bolt standing on its tip at the origin, its head up; without
a head it is a stud. Parameters `head` (`hex`, `socket`, `round`,
`countersunk`, `none`), `diameter` (of the shank, 0.06 m, 0.01 - 0.5; derived
0.04 - 0.08, step 0.01), `length` (from the tip to under the head, 0.3 m,
0.05 - 4; derived 0.2 - 0.5, step 0.05), `thread` (the share of the length
that is threaded, 0.6, 0.2 - 1; derived 0.4 - 1, step 0.1), `turns` (ridges of
the thread, 10, 4 - 40; derived 8 - 16), `thread_style` (`helix`, `rings`;
default `helix`) and `seed`; slot `steel`; connectors
base, seat (under the head, facing the tip) and nut (on the axis one
diameter up the shank, at most the length, facing the tip); a convex hull
body; no mirror plane (a helix has no mirror image); tags `decoration`,
`machine`, `fastener` and `bolt`.

- The thread covers 0.88 of the threaded length from a chamfered tip (a ring
  of 0.7 of the radius), a ridge to a turn: the root at 0.84 of the radius,
  the ridge at the radius, then plain up to the head.
- `helix`: a right-hand thread (U53). The root and the ridge rise a turn for
  each turn about Y from +X toward -Z, in triangles; the ridge winds one
  whole turn fewer than `turns`, from 0.65 of a turn over the tip, and at
  +X the thread starts in a planar face. Over its last turn the ridge runs out
  into the root, where a band rises to the plain shank. A turn has 16
  points, fewer above 28 turns (1880 over four times one more than the
  turns, at least 10), so that with the largest head the bolt stays within
  the kit's ceiling; the shank is four triangles for each point of a turn
  and each turn, and one turn more.
- `rings`: the shank is a lathe of ten points, a minor ring and a ridge ring
  to a turn.
- `hex`: a hexagon 1.8 diameters across the corners and 0.55 diameters high
  under a chamfer. `socket`: a round head 1.5 diameters wide and one diameter
  high with a socket in its top. `round`: a dome to 0.52 diameters.
  `countersunk`: a cone widening up to a flat top 1.84 diameters wide, sunk
  into the shank by up to 0.45 diameters (0.4 of a short bolt's length).

Budget 760 triangles (the defaults: 736).

### Nut

`machine.nut` is a nut lying flat, bored along Y for a bolt of its diameter.
Parameters `form` (`hex`, `square`, `wing`, `cap`), `diameter` (of the bolt,
0.06 m, 0.01 - 0.5; derived 0.04 - 0.08, step 0.01), `height` (0.05 m, 0.005
- 0.5; derived 0.04 - 0.06, step 0.01) and `seed`; slot `steel`; connectors
base, top (at the whole nut's top) and bolt (on the bored body's top, the
height up, facing up; a cap nut's on its base, facing down); a convex hull
body; mirror planes x and z;
tags `decoration`, `machine`, `fastener` and `nut`.

- `hex`: a bored prism of six sides, its corners 0.92 diameters from the
  axis. `square`: four sides, 1.05 diameters. `wing`: a round nut of twelve
  points with a wing leaning out of each side toward +X and -X; its top is
  1.9 heights. `cap`: the hexagon under a sleeve and a dome around a blind
  hole that goes on from the bore, 0.98 of its width (so that no point of
  the two parts meets), up to a ceiling 1.15 diameters over the base (at
  least 0.15 diameters over the hexagon). The sleeve is 0.76 diameters in
  radius, from inside the hexagon up to 0.45 diameters under the ceiling
  (at least to the hexagon's top), and the dome rises from there by 0.76
  diameters over rings of 0.66 and 0.38 diameters. Joined by its bolt
  connector to a bolt of its diameter it hangs upside down: the bolt's tip,
  a diameter beyond the connector, lies in the blind hole, and the thread's
  ridge stays inside the sleeve and the dome (U53).
- The bore is 0.84 diameters wide, the bolt thread's minor diameter.

Budget 90 triangles.

### Washer

`machine.washer` is a washer lying flat for a bolt of its diameter.
Parameters `form` (`flat`, `lock`, `toothed`), `diameter` (of the bolt, as
the nut's), `thickness` (of a flat washer, 0.01 m, 0.002 - 0.1; derived
0.008 - 0.012, step 0.002), `teeth` (of a toothed washer, 10, 6 - 16; derived
8 - 12) and `seed`; slot `steel`; connectors base and top; a convex hull body;
origin rule `base_contains_origin`; tags `decoration`, `machine`, `fastener`
and `washer`.

- `flat`: a ring of 16 points from 0.55 to 1.1 diameters in radius.
  `toothed`: the ring with `teeth` boxes standing out of it to 1.4
  diameters.
- `lock`: a split ring: a wire of a square section 2.5 thicknesses across
  over 340 degrees of a turn, its first seven of twelve stretches on the
  ground and the rest rising by 0.6 sections; its top is 1.6 sections over
  the ground.

Budget 260 triangles.

### Spring

`machine.spring` is a coil spring, its wire along a helix of twelve
stretches to a coil. Parameters `form` (`compression`, `extension`,
`torsion`), `diameter` (of the coil at the wire's middle, 0.2 m, 0.04 - 2;
derived 0.15 - 0.3, step 0.05), `wire` (its thickness, 0.02 m, 0.004 - 0.1;
derived 0.015 - 0.03, step 0.005), `coils` (8, 3 - 16, a compression spring's
two end coils among them; derived 5 - 10), `length` (a compression spring's
height, 0.4 m, 0.05 - 4, at least its closed height; derived 0.3 - 0.6, step
0.05) and `seed`; slot `steel`; connectors start and end (its two ends); a
convex hull body; origin rule `base_contains_origin`; tags `decoration`,
`machine` and `spring`.

- `compression`: standing on the origin, its length high; its first coil
  lies flat on the ground and its last flat at the top, the coils between
  rising evenly. Start is on the ground facing down, end at the top facing
  up.
- `extension`: lying along X, wound tight (a coil's pitch 1.08 wires), with
  a hook at each end: half a ring of the coil's radius from the coil's end
  point, the same wire as the coil through a mitred joint, in the plane
  through the axis and that point, so it rises from the coil's lowest side
  and reaches no lower than the coils. No point of a coil lies straight
  under the axis, so it rests on two lines. Start and end are the hooks' far
  points.
- `torsion`: standing, wound tight, with a leg 1.5 coil diameters long from
  each end along the wire's tangent, the wire's own first and last stretch:
  toward +Z on the ground and toward -Z at the top. Start and end are the legs' ends.

Budget 780 triangles.

### Bearing

`machine.bearing` is a rolling bearing lying flat. Parameters `form`
(`ball`, `roller`, `needle`), `bore` (0.1 m, 0.01 - 2, at most 0.8 of the
diameter; derived 0.08 - 0.12, step 0.02), `diameter` (outside, 0.3 m, 0.05 -
4; derived 0.25 - 0.4, step 0.05), `width` (along Y, 0.08 m, 0.01 - 1;
derived 0.06 - 0.1, step 0.02), `elements` (rolling elements, 10, 6 - 20;
derived 8 - 14) and `seed`; slots `steel` (the races and the elements) and
`metal` (the cage); connectors base and axis (the top); a convex hull body;
mirror plane z; tags `decoration`, `machine` and `bearing`.

- The inner race fills 0.22 of the space between the bore and the outside
  from the bore, the outer race 0.22 from the outside, both of 16 points; the
  cage is a thin ring between them over the middle of the width.
- The elements stand on the middle circle, each no wider than its
  neighbours leave room for: a ball of eight points 0.6 of the space across
  (at most 0.92 of the width), a roller 0.58 of the space, a needle 0.28;
  rollers and needles are 0.8 of the width long.

Budget 880 triangles.

### Pillow block

`machine.pillow_block` is a housing around a shaft along Z on a pedestal
and a footed base along X held by bolts. Parameters `bore` (the shaft's
diameter, 0.1 m, 0.02 - 2, at most the height; derived 0.08 - 0.12, step
0.02), `height` (of the axis, 0.15 m, 0.05 - 2; derived 0.15 - 0.2, step
0.05), `width` (the base along X, 0.4 m, 0.1 - 4; derived 0.4 - 0.5, step
0.1), `depth` (along the shaft, 0.1 m, 0.03 - 1; derived 0.08 - 0.12, step
0.02), `bolts` (`two`, `four`; default `two`) and `seed`;
slots `metal` and `steel` (the bush and the bolts' heads); connectors base
and axis (on the axis at the +Z face, facing +Z); a triangle mesh body;
mirror planes x and z; tags `decoration`, `obstacle`, `machine` and
`bearing`.

- The base is a quarter of the height high; the pedestal stops under the
  bush; the housing is a ring of 16 points around Z, 0.3 heights thick; the
  bush (slot `steel`) has twelve flats at the bore. A hexagonal bolt head
  stands on each end of the base, or two on each end half the depth
  apart.

Budget 320 triangles.

### Piston

`machine.piston` is a piston standing on its skirt at the origin.
Parameters `diameter` (0.3 m, 0.05 - 2; derived 0.2 - 0.4, step 0.05),
`height` (0.3 m, 0.05 - 2; derived 0.25 - 0.4, step 0.05), `rings` (piston
rings, 3, 1 - 4; derived 2 - 4) and `seed`; slots `metal` and `steel` (the
rings and the wrist pin); connectors base, crown (the top, facing up) and pin
(the wrist pin's middle, facing down); a convex hull body; mirror planes x
and z; tags `decoration`, `obstacle`, `machine` and `piston`.

- One lathe of 16 points (a side across +X): from the hollow's ceiling at
  0.62 of the height down its wall (0.8 of the radius), around the skirt's
  foot, up the outside over the rings, which stand 0.8 percent out of their
  grooves between 0.66 and 0.93 of the height (inside the flats of a
  cylinder of the diameter), to the crown's chamfer.
- The wrist pin lies along Z at 0.42 of the height, an octagon whose corners
  are 0.16 of the piston's radius from its axis (at most 0.12 of the
  height); its ends show on the skirt.

Budget 600 triangles.

### Connecting rod

`machine.connecting_rod` is a connecting rod lying flat, its eyes' axes
along Y: the small end at -X, an I-section shank, and the big end with its
cap's two bolts (slot `steel`) at +X. Parameters `length` (between the eyes'
axes, 0.5 m, 0.1 - 4; derived 0.4 - 0.7, step 0.1), `small_bore` (a piston's
wrist pin, 0.05 m, 0.01 - 1; derived 0.04 - 0.06, step 0.01), `big_bore` (a
crank pin, 0.08 m, 0.02 - 1; derived 0.08 - 0.1, step 0.01), `thickness`
(along Y, 0.06 m, 0.01 - 1; derived 0.04 - 0.08, step 0.02) and `seed`; slots
`metal` and `steel`; connectors base, small_end and big_end (the eyes'
middles, facing -X and +X along the rod, up along the eye's axis); a triangle
mesh body; origin rule `base_contains_origin`; mirror plane z; tags
`decoration`, `machine`, `piston` and `rod`.

- The eyes are rings, the small one of 12 points 1.8 bores wide, the big
  one of 16 points 1.6 bores wide; a bore's flats are its diameter across,
  so a pin of that diameter fits; on a short rod the bores are at most 0.2
  and 0.32 of the length, so the eyes stay apart. The shank is a web between
  two flanges from inside one eye's wall to inside the other's; the flanges
  start on the eyes' outlines at their outer sides.

Budget 400 triangles.

### Crankshaft

`machine.crankshaft` is a crankshaft along X on two stands. Parameters
`throws` (2, 1 - 6; derived 1 - 4), `stroke` (twice the crank pins' distance
from the axis, 0.2 m, 0.05 - 2; derived 0.15 - 0.3, step 0.05), `journal`
(the journals' and the pins' diameter, 0.08 m, 0.02 - 1; derived 0.08 - 0.1,
step 0.01), `web` (a web's thickness, 0.04 m, 0.01 - 0.5; derived 0.03 -
0.05, step 0.01), `phase` (`flat`, `cross`) and `seed`; slots `metal` (the
webs and the stands) and `steel` (the journals and the pins); connectors
start and end (the shaft's ends on the axis), base and pin (the first
throw's pin, facing up, up along the shaft); a triangle mesh body; origin
rule `base_contains_origin`; tags `decoration`, `obstacle`, `machine`,
`piston` and `crankshaft`.

- Main journals 1.5 journals long alternate with throws; a throw is two webs,
  each a box from its counterweight past the axis out to the pin, and a crank
  pin one journal long half the stroke off the axis. The length is the
  journals and the throws end to end.
- The first throw's pin stands up; the next throws follow half a turn apart
  (`flat`) or a quarter turn apart (`cross`). The axis stands high enough for
  the webs to clear the ground; a stand stands under each end journal.

Budget 400 triangles.

### Cylinder

`machine.cylinder` is a cylinder sleeve standing on its foot flange.
Parameters `bore` (the diameter of the piston it takes, 0.3 m, 0.05 - 2;
derived 0.2 - 0.4, step 0.05), `height` (0.5 m, 0.1 - 4; derived 0.4 - 0.7,
step 0.1), `fins` (cooling fins, 6, 0 - 10; derived 4 - 8), `wall` (0.03 m,
0.01 - 0.5; derived 0.02 - 0.04, step 0.01) and `seed`; slots `metal` (the
jacket) and `steel` (the liner); connectors base and top; a triangle mesh
body; mirror planes x and z; tags `decoration`, `obstacle`, `machine`,
`piston` and `cylinder`.

- The liner (a ring of 16 points, 0.6 walls thick, the full height) has a
  bore 1.03 of the piston's diameter, so a piston of the bore slides in it.
  The jacket stands around it from the ground to 0.97 of the height: a foot
  flange 1.5 walls out and 0.08 heights high, and `fins` fins 1.2 walls out,
  evenly between 0.14 and 0.94 of the height.

Budget 1100 triangles.

### Cam

`machine.cam` is a plate cam lying flat, its axis along Y through the
origin. Parameters `profile` (`eccentric`, `pear`, `heart`, `snail`; default
`pear`), `radius` (of the base circle, 0.15 m, 0.03 - 2; derived 0.1 - 0.2,
step 0.05), `lift` (how far the profile reaches beyond the base circle, 0.1
m, 0.01 - 1; derived 0.06 - 0.12, step 0.02), `thickness` (of the plate, 0.06
m, 0.01 - 1; derived 0.04 - 0.08, step 0.02), `bore` (0.06 m, at most the
radius; derived 0.04 - 0.08, step 0.02) and `seed`; slots `metal` (the
plate) and `steel` (the collar); connectors base, axis (the collar's top) and
follower (where the profile crosses +X, half the plate up, facing +X); a
triangle mesh body; origin rule `base_contains_origin`; tags `decoration`,
`machine` and `cam`.

- The plate is 32 points around the bore; its reach is the base circle's
  radius plus the lift toward +X. `eccentric`: a circle whose middle stands
  half the lift off the axis. `pear`: the base circle and a nose circle of
  0.45 radii on +X joined by the two lines that touch both. `heart`: the
  reach falls evenly to the far side and rises evenly back. `snail`: the
  reach falls evenly around the turn and steps back up before +X.
- The collar is a sleeve of 16 points in the bore, 1.7 times the bore across, standing
  0.6 thicknesses over the plate.

Budget 400 triangles.

### Cam follower

`machine.follower` is a cam follower standing on its contact at the origin
under a stem in a guide sleeve. Parameters `form` (`knife`, `roller`, `flat`;
default `roller`), `stem` (the stem's length over the head, 0.4 m, 0.05 - 4;
derived 0.3 - 0.5, step 0.1), `diameter` (of the stem, 0.04 m, 0.01 - 0.5;
derived 0.03 - 0.05, step 0.01) and `seed`; slots `steel` and `metal` (the
fork and the sleeve); connectors base, contact (at the origin, facing down)
and top (the stem's end); a convex hull body; origin rule
`base_contains_origin`; mirror planes x and z; tags `decoration`, `machine`,
`cam` and `follower`.

- `knife`: a wedge 1.5 diameters high whose edge, a land 0.1 diameters wide,
  lies along Z on the ground. `roller`: a roller of twelve sides twice the
  stem's diameter across whose axis lies along Z, a side of it on the
  ground, on an axle (slot `metal`, eight sides 0.4 diameters across) that
  spans the fork of two plates under a flange. `flat`: a disc three
  diameters across.
- The guide sleeve (slot `metal`) holds the stem between 0.45 and 0.75 of its
  length; its bore of ten points lies inside the stem's (0.96 of its
  diameter).

Budget 260 triangles.

### Camshaft

`machine.camshaft` is a camshaft along X on two stands. Parameters `lobes`
(4, 1 - 8; derived 2 - 6), `length` (of the shaft, 1 m, 0.2 - 8; derived 0.8
- 1.6, step 0.2), `lift` (of a lobe, 0.04 m, 0.005 - 0.5; derived 0.03 -
0.05, step 0.01), `radius` (of the lobes' base circle, 0.06 m, 0.02 - 1;
derived 0.05 - 0.08, step 0.01), `step` (degrees between a lobe and the next,
90, 0 - 180; fixed) and `seed`; slots `metal` (the stands) and `steel` (the
shaft and the lobes); connectors start and end (the shaft's ends on the
axis) and base; a triangle mesh body; origin rule `base_contains_origin`;
tags `decoration`, `machine`, `cam` and `camshaft`.

- The shaft is 0.6 radii thick, its axis 1.3 radii and the lift over the
  ground. The lobes are pear cams of 16 points, each 0.4 of its share of the
  length wide, evenly along the shaft and each turned by the step from the
  one before; the first lobe's nose stands up.

Budget 620 triangles.

## Future work

- [Level kit: more shapes](../plans/level_kit_more_shapes.md)
- [Level kit: extra shapes](../plans/level_kit_extra_shapes.md)
- [Level kit: machine elements](../plans/level_kit_machine_elements.md)
