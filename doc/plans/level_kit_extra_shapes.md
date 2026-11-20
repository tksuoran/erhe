# Level kit: extra shapes

Status: in progress

The inventory of shapes for step 63 of
`doc/plans/level_tools_implementation.md` ("Extra shapes of the
orchestrator's choice"): the topics and the shapes the orchestrator picks,
within rulings U34 and U36 of `doc/plans/level_tools.md`. All 36 shapes of
the inventory are built ("Build order") and described in
`doc/editor/level_kit_shapes.md`; what remains is the one
verification pass over every shape (`scripts/level_tools_verify.py` and the
editor's test suites) and the user's review of the choices the build made
alone. The inventory covers what step 62 leaves open: step 62 owns its five
categories (rock formations, tropical plants, palm trees, natural beach
features, architecture of historical cultures) and the unbuilt shapes of
R4.3 and R4.4 of `doc/plans/level_tools.md`; the unbuilt basic shapes of
R4.2 stay with step 12. This plan extends `doc/editor/level_kit_shapes.md`.

## Conventions of the tables

Each shape follows D1 of `doc/plans/level_tools_design.md` and R2 - R3 of
`doc/plans/level_tools.md`; the tables state only what differs per shape.

- Parameters: every shape has `seed`, and a curved shape has a fixed
  `segments` count; neither is listed. A listed length is in modules (m) or
  wall heights (wh) by default. A parameter in bold is in the shape's
  default derive mask; every other listed parameter is derivable, except
  those marked "fixed" (the ones walkability depends on, R2.10). A shape
  whose description names a light owns light nodes (prerequisite P2).
- Slots: `wood`, `metal`, `cloth`, `glow` and `water` are new slot names
  (P3); the others exist.
- Connectors: named as in D1.12, the first one being the connector a new
  piece attaches by.
- Body: `hull` is `Kit_collision::convex_hull`, `mesh` is
  `Kit_collision::triangle_mesh`, `none` is the new kind of P5.
- Layouts: the kit layout kinds the shape is made for (line, grid, ring,
  path, scatter).
- Geometry: the helper that builds it. `box`, `prism` and `polygon` are
  `Kit_mesh_builder::add_box()`, `add_prism()` and `add_polygon()`; `blob`
  names a lathe of rings around Y (the builder's ring helpers); `hull` is
  `erhe::geometry::shapes::make_convex_hull()`; `sweep` is
  `erhe::geometry::shapes::make_sweep()`; `beam` and `add_mesh` are the
  builder helpers of P4.
- Size, against the built shapes: S is a generator like `basic.block`,
  `architecture.pillar` or `nature.bush` (a few builder calls); M is like
  `architecture.arch`, `basic.stairs` or `nature.tree` (a computed profile
  or several parts, connectors that follow the values, a walkability
  check); L is like `nature.rock` (geometry operations, a second pass,
  care for the triangle ceiling over all seeds).
- Count maxima assume the raised triangle ceiling of P1.

## Topics

Six new kits and one shape in an existing kit, 36 shapes in all.

- Traversal (kit `traversal`, 4 shapes). The built kit and step 62 give
  ground, stairs, slopes and solid architecture; a level designer then
  lacks the light man-made structures that connect heights and cross
  gaps. These pieces turn separate walkable islands into one route, which
  the walkable area reads (R9, R10) then confirm.
- Architecture (existing kit, 1 shape). The tunnel is a stone module with
  an opening check, sharing the ring profile and the palette slots of
  `architecture.arch`, so it sits with the arch and the door wall.
- Props (kit `prop`, 8 shapes). A walled, floored level reads as empty
  until it is dressed. Props dress streets, yards and interiors of any of
  step 62's cultures, give scale to a space, and are the natural items of
  a weighted scatter and of grid stacks.
- Lights (kit `light`, 4 shapes). Fixtures that carry a light source give
  a level its night look and mark routes: a lamp post run along a path
  layout draws the eye down a street.
- Ruins (kit `ruin`, 5 shapes). Decay variants of the built architecture
  shapes with the same parameter names, so Replace carries the values
  (`doc/editor/level_tools.md` "Replace"): an intact wall run becomes a
  broken one in place. One seed yields many breaks, the strongest case
  for derive from seed outside the nature kit.
- Industrial (kit `industrial`, 6 shapes). A second style beside the
  pre-industrial kits: pipes that chain by connector snap, trusses, tanks,
  containers and rooftop detail.
- Harbour (kit `harbour`, 8 shapes). The man-made water edge, beside step
  62's natural beach: a quay and piers give a level a walkable shoreline
  with a clear edge, and boats, cranes, buoys and a lighthouse are
  silhouettes that read from far away.

## Inventory

### Traversal

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `traversal.ladder` | Two rails with rungs, upright or leaning toward -Z | height (1 wh), width (0.5 m), rung_spacing (0.3 meters, fixed), lean (0 - 20 degrees) | wood | bottom, top | mesh | line | beam | S |
| `traversal.rope_bridge` | Plank deck hanging in a parabola between its two ends, with rope rails | span (6 m), width (1 m, fixed), **sag** (0.03 - 0.1 of the span), plank_gap (fixed), **missing** (share of planks left out, 0 - 0.15), rail_height | wood, cloth | start, end | mesh | path, line | beam for planks, sweep for ropes | L |
| `traversal.scaffold` | One scaffold bay: four poles, cross braces, a plank deck per level | width (2 m), depth (1 m), levels (1 - 6), level_height (1 wh, fixed), braces (`none`, `single`, `cross`) | wood, metal | base, top, left, right, front, back | mesh | line, grid | prism, beam, box | M |
| `traversal.trestle` | Support under a deck: two splayed legs, a cap beam, cross braces | height (1 wh), width (1 m), splay (0 - 15 degrees), braces (0 - 3) | wood | base, top | mesh | path, line | beam | S |

The rope bridge and the scaffold decks are walkable at every derived value
(R2.10): each has a walkability check (deck slope at the bridge ends, gap
between planks). Border with R4.3: a straight plank deck on posts is the
"bridge deck" step 62 builds, so the inventory has none; a hanging deck
that sags and a multi-level frame are neither that nor the "platform".

### Architecture (existing kit)

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `architecture.tunnel` | Tunnel segment: a block with a bore along Z and a flat floor | length (2 m), bore_width (2 m, fixed), bore_height (0.8 wh, fixed), wall (0.3 meters), profile (`round`, `pointed`, `square`) | stone, floor | start, end, base | mesh | path, line | polygon rings extruded, as `architecture.arch` | M |

### Water

`nature.water`, the water surface the harbour shapes stand in and sit on,
is a shape of step 62: `doc/plans/level_kit_more_shapes.md` "Beach" holds
its entry and builds it with P5.

### Props

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `prop.crate` | Slatted wooden box | **width**, **height**, **depth** (0.5 - 1.25 m), slats (2 - 5), lid (`closed`, `open`, `none`) | wood | base, top | hull | grid, scatter, line | box | S |
| `prop.barrel` | Bulged barrel with hoops, upright or lying | **height** (0.7 - 1.2 meters), **radius**, bulge (0 - 0.2), hoops (2 - 4), pose (`upright`, `lying`) | wood, metal | base, top | hull | scatter, grid, ring | blob as lathe | S |
| `prop.pot` | Vessel family, hollow at the mouth | form (`jar`, `amphora`, `urn`, `bowl`, `planter`), **height** (0.3 - 1.2 meters), **width**, neck (0.3 - 0.9) | stone | base, top | hull | scatter, line, ring | polygon rings as a lathe with an inner wall | S |
| `prop.table` | Furniture family on four legs or two trestles | form (`table`, `bench`, `stool`, `shelf`), width, depth, height, legs (`posts`, `trestles`) | wood | base, top | mesh | line, grid, ring | box, beam | S |
| `prop.market_stall` | Counter, four posts and an awning | width (2 m), depth (1 m), height (0.8 wh), awning (`flat`, `sloped`, `peaked`), **overhang** | wood, cloth | base, start, end | mesh | path, line, ring | box, beam, polygon | M |
| `prop.cart` | Two-wheel hand cart resting on its handles | length (1.5 m), width (1 m), wheel_radius, **tilt** (0 - 15 degrees), sides (`open`, `rails`, `boards`) | wood, metal | base | mesh | scatter | box, prism, beam | M |
| `prop.well` | Ring wall with two posts and a crossbar or a gable roof | radius (0.75 m), wall_height (0.9 meters), roof (`none`, `crossbar`, `gable`) | stone, wood | base | mesh | scatter | polygon rings, beam, box | M |
| `prop.signpost` | Post with arrow boards at seeded headings and heights | height (0.8 wh), **arms** (1 - 4), arm_length, **lean** (0 - 6 degrees) | wood | base | mesh | scatter, path | prism, beam, polygon | S |

### Lights

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `light.lamp_post` | Post with a head on top or on one to four arms; one light per head (point; spot under a hood) | height (1.2 wh), arms (0 - 4), arm_length, head (`lantern`, `globe`, `hood`) | metal, glow | base | hull | path, line, ring | prism, beam, blob | M |
| `light.lantern` | Cage lantern: standing, hanging on a chain or on a wall bracket; one point light | mount (`standing`, `hanging`, `bracket`), size (0.3 meters), chain_length, sides (4 - 8) | metal, glow | base (standing), top (hanging), back (bracket) | hull | line, path, scatter | prism, beam | S |
| `light.fire` | Open flame family: brazier on three legs, wall torch, campfire in a stone ring; one point light | form (`brazier`, `torch`, `campfire`), size (0.6 meters), **flame** (height of the seeded flame blob) | metal, glow | base (torch: back) | hull | ring, line, scatter | blob, prism, beam, hull for campfire stones | M |
| `light.garland` | Wire sagging between its two ends with bulbs or flags along it; one point light at mid span | span (4 m), **sag** (0.05 - 0.15 of the span), count (3 - 24), item (`bulb`, `flag`, `paper_lantern`) | metal, glow | start, end | mesh | path, line | sweep, blob, polygon | M |

### Ruins

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `ruin.broken_wall` | `architecture.wall` with a seeded broken top outline in course steps | length, height, thickness (the wall's names and ranges), **intact** (0.3 - 0.8 of the area), break (`stepped`, `jagged`, `slope`), course (0.25 meters) | wall | start, end, base, front, back | mesh | path, line | outline polygon extruded through the thickness | M |
| `ruin.broken_pillar` | `architecture.pillar` broken off, with fallen drums beside the stump | profile, width, height (the pillar's), **standing** (0.2 - 0.7 of the height), **fallen** (0 - 3 drums) | trim | base | mesh | line, grid, scatter | prism, polygon for the slanted break | M |
| `ruin.broken_arch` | `architecture.arch` with part of its ring missing around a seeded position | span, rise, depth, thickness (the arch's), **gap** (0.15 - 0.5 of the ring), **gap_center** | trim | start, end, base | mesh | line | the arch ring of `make_arch_points()`, cut | M |
| `ruin.cracked_floor` | `basic.floor_slab` split into convex fragments, each slightly tilted or sunk, some missing | width, depth, thickness (the slab's), fragments (3 - 24), **tilt** (0 - 4 degrees), **missing** (0 - 0.2) | floor | the slab's six | mesh | grid | seeded straight cuts of the rectangle, polygon prisms | L |
| `ruin.rubble` | Heap of tumbled ashlar blocks within a round footprint | **radius** (0.5 - 2 m), **height** (0.2 - 0.6 of the radius), blocks (4 - 48), block_size | stone | none | hull | scatter | beam (turned boxes) | M |

The cracked floor stays walkable: its check bounds the height step between
neighbouring fragments by the agent's climb. The first four take every
parameter name of their intact shape with its type, which is what Replace
carries.

### Industrial

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `industrial.pipe` | Pipe piece with flanged ends | form (`straight`, `elbow`, `tee`), diameter (0.4 meters), length (2 m), bend_angle (45, 90 degrees), flanges (`none`, `ends`) | metal | start, end, branch (tee) | mesh | path, line | prism, sweep for the elbow | M |
| `industrial.tank` | Tank on legs or saddles, upright or lying | radius (1 m), height (1 wh), pose (`upright`, `lying`), ends (`flat`, `dome`, `cone`), bands (0 - 4) | metal, trim | base, top | hull | grid, line | blob as lathe, box | S |
| `industrial.truss` | Lattice beam of four chords and diagonals; upright it is a mast | length (4 m), width, height (0.5 m), bays (2 - 16), pattern (`warren`, `pratt`, `cross`) | metal | start, end, base, top | mesh | path, line | beam | M |
| `industrial.container` | Shipping container with corrugated sides and an end door | length (`short`, `standard`, `long`), ribs per module (2 - 6), door (`closed`, `ajar`, `open`), **door_angle** | metal, trim | base, top, front, back | mesh | grid, line, scatter | polygon strips, box | M |
| `industrial.vent` | Rooftop unit family | form (`louvre_box`, `fan`, `stack`, `duct_elbow`), **width**, **height** (0.5 - 1.5 m), slats (3 - 8) | metal | base | hull | scatter, grid | box, prism, beam | S |
| `industrial.antenna` | Mast with a seeded set of elements | form (`dish`, `whip`, `array`), height (1 wh), **elements** (1 - 5), **tilt** (0 - 40 degrees) | metal | base | mesh | scatter | prism, beam, blob as dish | M |

A container's top and a tank's flat top are walkable by construction; a
stack of containers joins base to top.

### Harbour

| Shape | Description | Parameters | Slots | Connectors | Body | Layouts | Geometry | Size |
|---|---|---|---|---|---|---|---|---|
| `harbour.quay_wall` | Quay segment: a battered wall with a coping, walkable on top, timber fenders on its water face | length (2 m), height (1 wh), thickness (1 m), batter (0 - 10 degrees), fenders (0 - 4) | stone, wood | start, end, base, top | mesh | path, line | polygon profile extruded along X, beam | M |
| `harbour.pier` | Deck on piles that reach below it into the water, with cross braces | length (2 m), width (2 m, fixed), pile_depth (1 wh), pile_spacing, braces (`none`, `cross`), **plank_jitter** | wood | start, end, left, right | mesh | path, line, grid | prism, box, beam | M |
| `harbour.mooring` | Mooring family | form (`bollard`, `cleat`, `post`, `pile_cluster`), size (0.5 meters), **lean** (0 - 8 degrees), rope (`none`, `coil`) | metal, wood | base | hull | line, path, scatter | blob as lathe, prism, sweep for the coil | S |
| `harbour.boat` | Open hull family with thwarts, afloat or beached on its side | form (`rowboat`, `skiff`, `barge`), **length** (3 - 8 meters), beam, sheer (0 - 0.3), thwarts (0 - 4), **heel** (0 - 25 degrees) | wood, trim | base | mesh | scatter, line | station outlines lofted along the keel as polygon strips | L |
| `harbour.crane` | Quay crane: a post or a lattice tower with a jib at a seeded heading, hook hanging | form (`derrick`, `tower`), height (1.5 wh), jib_length, **jib_heading**, jib_angle (10 - 60 degrees) | metal, wood | base | mesh | line | prism, beam | M |
| `harbour.buoy` | Float with a top mark; one point light | form (`can`, `cone`, `sphere`, `spar`), size (1 meters), **tilt** (0 - 12 degrees), bands (1 - 3) | metal, trim, glow | base | hull | path, scatter | blob as lathe, beam | S |
| `harbour.lighthouse` | Tapered banded tower with a gallery and a lantern room; one spot light | height (4 wh), base_radius (2 m), taper (0.5 - 0.9), bands (1 - 6), gallery (`none`, `rail`) | wall, trim, glow | base, top | mesh | - | blob as lathe in band slots, prism, beam | M |
| `harbour.cargo` | Pallet with a seeded stack of goods | load (`crates`, `sacks`, `barrels`, `mixed`), **layers** (1 - 3), **fill** (0.5 - 1), width (1 m) | wood, cloth | base, top | hull | grid, scatter, line | box, blob | M |

The quay top and the pier deck are walkable at every derived value; the
pier's check bounds the gap between planks. A path layout of
`harbour.buoy` marks a channel along a curve.

- `harbour.boat` needs a "float on water" rule of its own: water is no snap
  to surface target (`doc/editor/level_tools.md` "Surface placement").
- `harbour.buoy` needs the same "float on water" rule.

## Prerequisites

What the shapes need that is not built, each sized as the shapes are.

- P1 Triangle ceiling (S), landed with step 62's first batch
  (`doc/plans/level_kit_more_shapes.md` "Build order"). The per-shape limit at
  default parameters is `Kit_definition::triangle_ceiling`, 2000 for every
  kit in `kit_shape_library.cpp`, R2.6 restated; each shape states its own
  `Kit_shape_definition::triangle_budget` below it. `editor_level_tests`
  (`test_kit_shapes.cpp`) reads both: the count at the defaults against the
  budget, the budget against the ceiling, and every rock form and palm
  species against the ceiling. Reason: the largest shapes here are many boxes of 12 triangles
  (a rope bridge of 40 planks with four swept ropes is about 1100, a
  six-level cross-braced scaffold about 900), and 2000 holds them at their
  defaults with margin while a scattered run of a hundred pieces stays
  within what one imported prop costs. Budgets of the built shapes stay.
- P2 Light on a piece (M), built as D3.15 of
  `doc/plans/level_tools_design.md` (U34, U36; behavior in
  `doc/editor/level_tools.md` "Lights on pieces"):
  - Frames: a shape states light frames from its values, as it states
    connectors: a name, a position and a direction in the piece's space.
    Each frame is a child node of the piece node holding an
    `erhe::scene::Light`; the kit sets that node's name and local
    transform and nothing else of it. A shape states at most four frames
    (the lamp post's arms; a garland has one, at mid span).
  - Light values: type, color, intensity, range, cone angles and shadow
    casting are the light node's own properties, edited in the Properties
    window as any light's. The shape states only the values a new light
    node starts with: point (spot for a hood and the lighthouse), a warm
    white, a range of 8 meters (the lighthouse 100), shadow casting off.
  - Keeping edits: a light node is matched to a frame by its name. Any
    regeneration of the piece (parameter change, follows-kit refresh,
    load, a kit layout regenerating a kept child) moves the matched node
    to its frame and leaves its light properties as they are; a frame
    without a node gets a new one with the starting values, and a node
    without a frame is removed, all in the operation's undo entry.
    Replace does the same against the new shape's frame names, so a lamp
    post replaced by a lantern keeps its edited light where the names
    agree and loses it where the new shape has no such frame.
  - Copies: a duplicate and a group duplicate copy the light nodes with
    their edited properties. A mirror moves each node to the mirrored
    frame, direction included. New children of a scatter or a kit layout
    start with the starting values; the properties of one child's light
    are copied to the others by Copy and Paste Properties.
  - Count: every lit piece makes its lights, without a limit (U36). Baking
    lights to vertices is a possible later direction, not planned here.
- P3 Slots and palette (S), built. The slot names `wood`, `metal`, `cloth`,
  `glow` and `water` have their `Kit_slot_palette`: `water` is Kit Water,
  alternative Kit Shallow Water (with `nature.water`); `wood` is Kit Wood,
  alternatives Kit Vermilion, Kit Dark Wood and Kit Bark (with
  `architecture.torii`); `glow` is Kit Glow, alternative Kit Ember (with
  `architecture.stone_lantern`); `metal` is Kit Metal, alternatives Kit Rust,
  Kit Dark Wood and Kit Stone, and `cloth` is Kit Cloth, alternatives
  Kit Red Cloth, Kit Blue Cloth and Kit Straw (with build-order lines 1 - 8).
  Kit Clay is an alternative of `stone`. `Kit_palette_material` has a metallic
  value (Kit Metal, Kit Rust) and an emissive color (Kit Glow, Kit Ember).
- P4 Builder helpers (S). `Kit_mesh_builder::add_beam(a, b, width,
  height)`, a box between two points, for every `beam` (landed with
  `architecture.torii`);
  `Kit_mesh_builder::add_mesh(const GEO::Mesh&)`, which takes a generated
  mesh's facets under the current slot, for every `sweep` joined to other
  parts (landed with `nature.palm`; a quad becomes two triangles).
- P5 Pieces without a body (S), landed with `nature.water`.
  `Kit_collision::none`: the piece gets no physics body (`Brush_body::none`),
  and the navigation build leaves it out of its input (the authored flag
  `pass_through`), so `nature.water` is neither stood on nor
  walkable; Replace moves the body and the flag with the collision kind. The
  mesh checks accept a shape stated to be a surface (R2.5,
  `Kit_mesh_kind::surface`) as an open mesh.
- Navigation links ("Later" of `doc/plans/level_tools.md`): the reachable
  area crosses a `traversal.ladder` only with a link; before that the
  walkable reads report its two ends as separate islands.
- Per-piece random scale for scatter (step 39 of
  `doc/plans/level_tools_implementation.md`): until it is built the
  scatter-suited shapes vary by their derived sizes.

## Build order

The inventory is built in five batches in this order: P1 first, then the
first eight by value per effort, then the rest. The tables of "Inventory"
state what was planned; where a built shape differs, the batch's paragraph
below says how.

Built: lines 1 - 8 (`kit_shapes_extra_01_08.cpp`), with the kits `prop`,
`ruin`, `traversal`, `harbour` and `light` and the rest of P3. A built shape
differs from its row of the inventory in these points:

- `ruin.broken_wall` has a count `steps`, the columns its top falls in, so
  that its topology follows from integers; `course` is the height of a step
  and of a tooth. It has the wall's connectors but `top`.
- `prop.barrel`, `harbour.pier`, `harbour.mooring` and `light.lamp_post` have
  a count `segments`, the points around their round parts.
- `ruin.rubble` lays its blocks on a low mound of debris, so that no block
  floats at any count and height.
- `traversal.ladder` and `harbour.pier` take their rung, plank and bent counts
  from lengths, as the stairs take their step count.
- `harbour.pier` has `width` in meters, as `architecture.bridge_deck`.
- `harbour.mooring` draws its rope in slot `wood`: every form has facets of
  both slots with and without a rope.
- `light.lamp_post` names its light frames `light_1` to `light_4`.

Built: the next eight (`kit_shapes_extra_09_16.cpp`): `harbour.quay_wall`,
`industrial.pipe` with the kit `industrial`, and the rest of the props
(`prop.pot`, `prop.table`, `prop.market_stall`, `prop.cart`, `prop.well`,
`prop.signpost`). The next shape to build is `ruin.broken_pillar`. A built
shape differs from its row of the inventory in these points:

- `harbour.quay_wall` keeps its top the thickness wide and lets the foot of
  its water face stand out by the batter, so the top is walkable at every
  batter; a timber kerb runs along the water's edge, so the piece has wood
  without fenders. Its origin is under the middle of the top.
- `industrial.pipe` has `bend_angle` as an angle of 15 - 90 degrees, derived
  as 45 or 90, and states `branch` at every form: a tee's branch end, a
  straight pipe's side, the outside of an elbow's bend.
- `prop.pot`, `industrial.pipe`, `prop.cart` and `prop.well` have a count
  `segments`.
- `prop.table` has its sizes in meters; a bench has a back, and `legs`
  `trestles` are boards under a stool and beside a shelf.
- `prop.market_stall` has `overhang` in meters, at the front and the back
  only, so stalls join side to side.
- `prop.cart` stands on two legs under the bed's front at every tilt; the
  tilt is at most what keeps the handles' ends over the ground.
- `prop.well` has a wooden bucket on its rim at every roof, so the piece has
  wood without a roof, and a windlass under a crossbar and a gable.

Built: the third eight (`kit_shapes_extra_17_24.cpp`): `ruin.broken_pillar`,
`ruin.broken_arch`, `light.lantern`, `light.fire`, `harbour.buoy`,
`traversal.trestle`, `traversal.scaffold` and `architecture.tunnel`. The next
shape to build is `harbour.cargo`. A built shape differs from its row of the
inventory in these points:

- `ruin.broken_arch` is two arms, each of a segment more than half the arch's
  `segments`, cut across the ring at the breaks; an arm is at least 0.08 of
  the ring, and `gap_center` lies within 0.25 - 0.75 of the ring.
- `light.lantern` states base, top and back at every mount, and
  `light.fire` base and back at every form: a shape's connector names are the
  same at every value.
- `light.fire` has a third slot `stone` (a brazier's plinth, the stone a torch
  hangs on, a campfire's ring); a campfire stands under a tripod of rods, so
  every form has metal.
- `harbour.buoy` stands on its foot at the origin, has twelve rings up its
  float whatever its `bands`, and a count `segments`.
- `traversal.scaffold` has `level_height` in meters (2.4 m), and its braces
  and base plates in slot `metal`; a guard rail runs over the top deck.
- `architecture.tunnel` has its bore along X, so that segments chain along a
  path and a line as the bridge deck does, `bore_height` in meters (2.4 m),
  and the count `segments` of its head.

Built: the fourth eight (`kit_shapes_extra_25_32.cpp`): `harbour.cargo`,
`harbour.crane`, `harbour.lighthouse` and the rest of the industrial kit
(`industrial.tank`, `industrial.truss`, `industrial.container`,
`industrial.vent`, `industrial.antenna`). The next shape to build is
`light.garland`; `traversal.rope_bridge`, `ruin.cracked_floor` and
`harbour.boat` follow it and end the inventory. A built shape differs from its
row of the inventory in these points:

- `harbour.cargo` has four goods to a layer at every `fill`, which is the
  share of the pallet they cover; a mixed load is crates under sacks under
  barrels, and a folded tarp lies on every load, so every load has cloth.
- `harbour.crane` takes `jib_heading` as a parameter in the default mask and
  draws nothing from the seed; `jib_length` is in meters. The derrick's mast
  and jib are wood and its lines and hook metal; the tower has a timber cab.
- `harbour.lighthouse` has a count `segments` and a door toward +X; its spot
  light stands beside the lantern room and shines level toward +X.
- `industrial.tank` stands on legs or saddles of slot `trim`, so a tank
  without bands has trim; it has a count `segments`.
- `industrial.truss` has its lattice in its two faces toward +Z and -Z, and
  struts across at the nodes.
- `industrial.container` is 2.4 m wide and 2.6 m high and 3, 6 or 12 m long;
  `ribs` counts the ribs of a side to every 3 m, and its front and back
  connectors are its long sides.
- `industrial.vent` has `width` and `height` only (its depth follows the
  width), and `slats` are the louvres, a fan's blades or a stack's struts.
- `industrial.antenna` has its whips over a short mast, the first up to the
  height.

Built: the last four (`kit_shapes_extra_33_36.cpp`): `light.garland`,
`traversal.rope_bridge`, `ruin.cracked_floor` and `harbour.boat`, which end
the inventory. A built shape differs from its row of the inventory in these
points:

- `light.garland` has a third slot `cloth`: its flags and a ribbon at each
  end, so every item has cloth; a garland of flags has a lamp on a cord at
  mid span, so every item has glow. Its wire is a chain of sheared boxes, not
  a sweep. Its origin is under mid span at its lowest point, and its ends are
  the connectors.
- `traversal.rope_bridge` takes its plank count from the span and the plank
  gap, has `width` and `plank_gap` in meters, and has its ropes in slot
  `cloth` as chains of sheared boxes. A missing plank lies loose on the next
  plank and leaves a gap, so the plank count is the same at every seed. Its
  origin is under mid span at its lowest point.
- `ruin.cracked_floor` cuts the slab into rows and each row into fragments,
  so every fragment has four corners at every seed; a missing fragment lies
  deep (0.85 of the thickness, at most 0.3 m) instead of being left out.
- `harbour.boat` stands on two sleepers at every heel, since its base is
  level; its skins are triangles, its ends are closed by boards, and the
  sheer and the heel are parameters the generator draws nothing for.

1. `prop.crate` - S, brings P3's `wood`; stacks by base and top, the
   plainest scatter and grid item.
2. `prop.barrel` - S; with the crate the first weighted scatter of
   dressing.
3. `ruin.broken_wall` - shows Replace carrying an intact wall run's values
   and derive from seed on an architecture shape.
4. `ruin.rubble` - brings `add_beam()`; the scatter item at the foot of
   every ruin.
5. `traversal.ladder` - S with `add_beam()`; the first vertical route
   beside stairs.
6. `harbour.pier` - a walkable route along a path layout over water, the
   start of the harbour.
7. `harbour.mooring` - S; line and path dressing of every pier and quay.
8. `light.lamp_post` with P2 - the first lit shape; every light shape
   after it is S or M.

Then, in this order, eight to a batch: `harbour.quay_wall`, `industrial.pipe`, the rest of
the props, `ruin.broken_pillar` and `ruin.broken_arch`, `light.lantern`,
`light.fire` and `harbour.buoy`, `traversal.trestle`, `traversal.scaffold`
and `architecture.tunnel`, `harbour.cargo`, `harbour.crane` and
`harbour.lighthouse`, the rest of the industrial kit; last the shapes that
wait for `add_mesh()` or are L: `light.garland`, `traversal.rope_bridge`,
`ruin.cracked_floor`, `harbour.boat`.

## Future work

- A science fiction kit (U34).

## Open questions

None: U34 and U36 answer the questions this inventory raised.
