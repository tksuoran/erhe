# Level kit: machine elements

Status: in progress

The inventory of the kit `machine` ("Machine Elements"): gears, fasteners,
pistons, bearings, springs, and cams and followers, as the user asked for
them. It follows `doc/plans/level_kit_extra_shapes.md` in form and in what a
shape is (D1 of `doc/plans/level_tools_design.md`, R2 - R3 of
`doc/plans/level_tools.md`), and extends `doc/editor/level_kit_shapes.md`.
Every shape of the inventory is built, tested and described there
("Machine kit"); the changes the user asked for are step 84 of
`doc/plans/level_tools_implementation.md`.

## Conventions of the tables

- Scale: every length is in meters and none follows the level's module or
  wall height. The defaults are oversized decorative machine parts that read
  at level scale: a gear half a meter across, a bolt 0.3 m long. A part is
  made larger or smaller by its own lengths.
- Pose: a part lies or stands on the ground as a prop would: a gear, a cam
  and a bearing lie flat with their axis along Y; a bolt stands on its tip, a
  piston on its skirt, a compression spring on its end coil.
- Parameters: every shape has `seed` (no generator of the kit draws from
  it). A count is an integer parameter, and a mesh's topology follows from
  the integers alone (R3.7). A parameter in bold is in the shape's default
  derive mask; every other listed parameter is derivable, except those marked
  "fixed".
- Slots: `metal` is dark cast iron (Kit Metal); `steel` is a bright machined
  surface (new: default Kit Steel, alternatives Kit Brass, Kit Metal, Kit
  Rust). Kit Steel and Kit Brass are new palette materials and also
  alternatives of `metal`.
- Connectors: where parts mate. `base` is at the origin facing -Y. `axis` is
  a shaft or bore axis. `mesh` is a point on a pitch circle or pitch line
  where teeth engage: every `mesh` connector has a tooth centered a quarter
  pitch from it toward -(up x outward), so two parts of one `module` joined
  mesh to mesh have their teeth half a pitch apart and engage, whatever
  their tooth counts.
- Body: `hull` is `Kit_collision::convex_hull`, `mesh` is
  `Kit_collision::triangle_mesh`.
- Geometry: `lathe` is rings about an axis; `prism` is an outline extruded;
  `helix` is a tube of four corners along a helix, in triangles; `box` and
  `beam` are the builder's.
- Size: S, M and L as in the extra shapes document.

## Inventory

### Gears

| Shape | Description | Parameters | Slots | Connectors | Body | Geometry | Size |
|---|---|---|---|---|---|---|---|
| `machine.gear` | Spur gear lying flat: a rim of teeth around a body with a bored hub | teeth (8 - 40), module (0.03), face_width (0.08), bore (0.08), body (`solid`, `web`, `spokes`), spokes (3 - 8) | metal | base, axis, mesh | mesh | lathe, prism for each tooth, box | M |
| `machine.rack` | Gear rack: a bar along X with teeth toward +Z, lying flat | teeth (4 - 40), module (0.03), face_width (0.08), height (0.08) | metal | start, end, base, mesh | mesh | box, prism for each tooth | S |
| `machine.bevel_gear` | Bevel gear: teeth on a cone, for shafts at a right angle | teeth (8 - 32), module, face_width, cone (30 - 60 degrees), bore | metal | base, axis, mesh | mesh | lathe, prism for each tooth, tapered | M |
| `machine.worm` | Worm on a shaft, or the worm wheel it drives | form (`worm`, `wheel`), module, diameter (0.12, of the worm), starts (1 - 3), turns (2 - 6), teeth of the wheel (16 - 40) | metal, steel | base, axis, mesh | mesh | helix for the thread, lathe, prism | L |
| `machine.pulley` | Belt pulley with a groove, or a chain sprocket | form (`pulley`, `sprocket`), diameter, width, grooves (1 - 4), teeth of a sprocket (8 - 32), bore | metal | base, axis | mesh | lathe, prism for each tooth | S |

Two gears, a gear and a rack, and a worm and its wheel mesh when
their `module` is equal: the pitch is pi times the module, and a gear's pitch
diameter is its teeth times the module (16 teeth at 0.03: 0.48 m).

### Fasteners

| Shape | Description | Parameters | Slots | Connectors | Body | Geometry | Size |
|---|---|---|---|---|---|---|---|
| `machine.bolt` | Bolt standing on its tip, head up; without a head a stud | head (`hex`, `socket`, `round`, `countersunk`, `none`), diameter (0.06), length (0.3), thread (share of the length that is threaded, 0.2 - 1), turns (4 - 40) | steel | base, seat, nut | hull | lathe | S |
| `machine.nut` | Nut lying flat, bored through; a cap nut is closed on top | form (`hex`, `square`, `wing`, `cap`), diameter (0.06), height | steel | base, top | hull | prism with a bore, lathe, box | S |
| `machine.washer` | Washer: flat, a split lock washer, or a toothed one | form (`flat`, `lock`, `toothed`), diameter, thickness, teeth (6 - 16) | steel | base, top | hull | lathe, prism | S |

A bolt's thread is a right-hand helix of `turns` turns (`thread_style` `rings`
gives a ridge to a turn instead; step 84). The bolt's `nut`
connector faces its tip, a little up the thread; a nut joined to it by its
`top` sits on the thread's end. `seat` is under the head, facing the tip: the
surface the head is tightened against.

### Pistons

| Shape | Description | Parameters | Slots | Connectors | Body | Geometry | Size |
|---|---|---|---|---|---|---|---|
| `machine.piston` | Piston standing on its skirt: a crown over ring grooves, hollow from below, with a wrist pin | diameter (0.3), height (0.3), rings (1 - 4) | metal, steel | base, crown, pin | hull | lathe, lathe along Z for the pin | M |
| `machine.connecting_rod` | Connecting rod lying flat: a small end, an I-section shank and a split big end with two bolts | length (0.5, between the two eyes), small_bore, big_bore, thickness | metal, steel | base, small_end, big_end | mesh | prism with a bore for each eye, box | M |
| `machine.crankshaft` | Crankshaft along X on two stands: main journals, webs with counterweights and crank pins | throws (1 - 6), stroke (0.2), journal (0.08), web, phase (`flat`, `cross`) | metal, steel | start, end, base, pin | mesh | lathe along X, prism for each web | L |
| `machine.cylinder` | Cylinder sleeve standing: a liner with cooling fins and a flange | bore (0.3), height (0.5), fins (0 - 10), wall | metal, steel | base, top | mesh | lathe | S |

A piston's `pin` is at the wrist pin's middle, facing down; a connecting
rod's `small_end` joins it, and its `big_end` a crankshaft's `pin`.

### Bearings

| Shape | Description | Parameters | Slots | Connectors | Body | Geometry | Size |
|---|---|---|---|---|---|---|---|
| `machine.bearing` | Rolling bearing lying flat: an outer and an inner race around rolling elements in a cage | form (`ball`, `roller`, `needle`), bore (0.1), diameter (0.3), width (0.08), elements (6 - 20) | steel, metal | base, axis | hull | lathe | M |
| `machine.pillow_block` | Pillow block: a housing on a footed base with two bolt holes, around a bearing seat | bore (0.1), height (to the axis), width, depth (along the shaft), bolts (2, 4) | metal, steel | base, axis | mesh | prism with a bore, box, lathe | M |

A bearing's races keep the proportions of a real one: the rolling elements'
diameter is 0.6 of the space between bore and outside, and the races share
the rest.

### Springs

| Shape | Description | Parameters | Slots | Connectors | Body | Geometry | Size |
|---|---|---|---|---|---|---|---|
| `machine.spring` | Coil spring: compression (standing, its end coils closed flat), extension (lying along X, tightly wound, a hook at each end) or torsion (standing, tightly wound, a leg from each end) | form (`compression`, `extension`, `torsion`), diameter (0.2, of the coil's middle), wire (0.02), coils (3 - 16), length (0.4, of a compression spring) | steel | start, end | hull | helix | L |

The wire runs along a real helix: twelve stretches to a coil.

### Cams and followers

| Shape | Description | Parameters | Slots | Connectors | Body | Geometry | Size |
|---|---|---|---|---|---|---|---|
| `machine.cam` | Plate cam lying flat: a profile around a bored hub | profile (`eccentric`, `pear`, `heart`, `snail`), radius (0.15, of the base circle), lift (0.1), thickness (0.06), bore (0.06) | metal, steel | base, axis, follower | mesh | prism with a bore | M |
| `machine.follower` | Follower on a stem in a guide: a knife edge, a roller or a flat face | form (`knife`, `roller`, `flat`), stem (length), diameter; always in a guide sleeve | steel, metal | base, contact, top | hull | lathe, box, lathe for the roller | S |
| `machine.camshaft` | Camshaft along X on two stands: a shaft with pear lobes at stepped angles | lobes (1 - 8), length, lift, radius, step (degrees between two lobes) | metal, steel | start, end, base | mesh | lathe along X, prism for each lobe | M |

A cam's `follower` connector is where its profile crosses +X, facing +X: a
follower's `contact` joins it.

## Prerequisites

- P1 Kit (S), built: the kit `machine` with the display name "Machine
  Elements" and a triangle ceiling of 2000.
- P2 Slots and palette (S), built: the slot `steel` and the palette
  materials Kit Steel and Kit Brass.
- P3 Bored prism (S), built: a prism of m sides with a round bore of 2m
  points (the hexagon and the square of a nut), and the helix tube (a
  spring's wire), both in `kit_shapes_machine_01_08.cpp`.

## Build order

1. `machine.gear` - the shape the kit is known by; brings the tooth profile
   and the `mesh` connector rule.
2. `machine.rack` - S after the gear; shows the rule on a pitch line.
3. `machine.bolt` - the first fastener; brings the `steel` slot.
4. `machine.nut` - with the bolt the first joined pair; brings the bored
   prism.
5. `machine.spring` - brings the helix.
6. `machine.bearing` - lathe only.
7. `machine.piston` - the first of the crank train.
8. `machine.cam` - the first of the cam train.

9. `machine.connecting_rod`, 10. `machine.crankshaft` - the crank train joins
   by its connectors.
11. `machine.follower`, 12. `machine.camshaft` - the cam train.
13. `machine.bevel_gear`, 14. `machine.worm`, 15. `machine.pulley`,
    16. `machine.washer`, 17. `machine.cylinder`, 18. `machine.pillow_block`.

Built: lines 1 - 8 (`kit_shapes_machine_01_08.cpp`) and lines 9 - 18
(`kit_shapes_machine_09_18.cpp`), with P1 - P3.

Remaining: test rows (`kit_shape_rows_machine_09_18.cpp`), shape tests,
measured triangle budgets and renders of lines 9 - 18; pin and component
rows of lines 1 - 8.

## Future work

- A leaf spring (a stack of leaves in a shackle).
- Rivets, a split pin and a key.
- A chain and a belt as kit layouts along a path between sprockets and
  pulleys.
- Turning parts: gears that turn each other in the editor's animation.

## Open questions

None: U49 and U53 of `doc/plans/level_tools.md` answer Q1 - Q3 (oversized
defaults, flat pose, helical thread); the changes are step 84 of
`doc/plans/level_tools_implementation.md`.
