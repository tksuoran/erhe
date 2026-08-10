---
name: erhe-creations
description: Build an MCP-driven showcase scene ("creation") in the erhe editor. Read BEFORE starting, revising, or debugging any creation in scripts/creations/ - covers the workflow (self-launching scripts, screenshot iteration in either build, windowed showing), the mandatory scene-graph hierarchy rules, and the MCP editor gotchas (transforms, materials, lighting); domain recipes (vegetation, physics rigs, CSG/hulls, settling, sweep blades) live in references/*.md indexed inside. Revise this document (and the matching reference file) at the end of each creation session with new learnings.
---

# Editor AI Creations

Showcase scenes built by driving the in-editor MCP server from Python
scripts in `scripts/creations/`. Each script is self-contained and
reproducible: it launches the editor, builds one scene, frames the
camera, screenshots and saves the scene.

**Maintenance contract:** read this document before any creation work;
at the end of a session, fold new facts in (and prune anything they
made obsolete). This file is the workflow contract + gotcha index;
stable domain recipes live in `references/*.md` (see "Domain recipes"
below) - fold workflow/tooling facts HERE and domain facts into the
matching reference file (new domain = new file + one index line here).
This skill is the canonical home for creation knowledge - agent memory
and prompt_queue.txt only point here.

## Existing creations

`creation_1_conway_cathedral` … `creation_17_rock_piles` (henge,
reef, robots, ragdoll, glass audience, sandbox + L-system oak, forest
glade, monster portal island, UAP hangar, windswept glade = glade +
physics foliage + wind, spider sentinel = motor-held STANDING ragdoll,
tree garden = 28 Finnish species via lsystem_trees.py, sail ships =
CSG-carved convex-hull hulls + FFD-billowed sails, rockfall =
physics-settled rock piles + staged cairn under the manual clock).
Look at the two or three most recent scripts before writing a new one -
they carry the current idioms.

## Workflow

- Every script self-launches the WINDOWED Release editor with
  `--commands config/editor/commands_empty.json` (no default scene; the
  creation scene is the ONLY scene). Flags from `common.standard_args`:
  `--reuse` (attach to running editor), `--editor-exe` (pick a build),
  `--pause N` (recording pause after first visible mesh, default 10 s),
  `--no-save` (optional: skip `save_scene`; windowed saves work since
  the exporter's content-flag filter, 2026-08-08).
- **Iterate with screenshots**: `capture_screenshot` works in BOTH
  builds since 2026-08-08 (windowed captures the editor's own composited
  frame -- one frame of extra latency, handled inside the MCP server).
  Iterating against the WINDOWED build is now fine and needs no ini
  ritual; the headless build (`--editor-exe
  build_vs2026_vulkan_headless/src/editor/Release/editor.exe`) is still
  useful when the display is off or the user is using the machine.
  Judge the PNG in `logs/creations/`, fix, rerun. Expect 2-4
  iterations; composition problems (occlusion, framing, washed
  lighting) are the usual finds.
- HEADLESS ONLY: big viewports need
  `config/editor/desktop_window_imgui_host_imgui.ini` seeded with
  `[Window][Viewport_window N]` `Pos=8,28` `Size=2288,1160` for N=2..12
  BEFORE launch. **Back up the user's ini first and restore it after
  the headless iterations** (previous sessions keep a backup at
  `%TEMP%\erhe_imgui_ini_backup.ini`). Windowed runs use the user's
  real layout and skip all of this.
- Iteration modes (2026-08-08): `--reuse` attaches to the running
  editor AND closes the previous run's scenes first (`--keep-scenes`
  opts out) - iterate without relaunching or leaking scenes.
  `--reframe <glb>` skips the build: it load_scenes the saved .glb and
  runs only the script's camera/screenshot stage - composition
  iteration in seconds instead of a full rebuild. Generic since
  2026-08-09: hoist the shot list to a module SHOTS constant and put
  `if common.reframe(args, title, base_path, SHOTS): return` at the
  top of main() (creation_16 is the pattern; creation_14 has the older
  hand-rolled version). EVERY new creation should wire this.
- **`--only <object>` partial rebuild** (2026-08-09; creation_16 is the
  pattern): attach to the running editor's scene (implies reuse +
  keep-scenes), `c.delete_nodes(names=[obj])` the object's root group,
  rebuild only that object, close-up screenshot via `shot_relative`.
  Structure main() so it supports this: objects in a name -> builder
  map, scene-level setup (ambience/lights/ground) gated on `not only`,
  materials through `c.ensure_material` (returns the existing library
  material by name instead of stacking suffixed duplicates). Measured
  on creation 16: 182 vs 419 MCP calls for a one-ship change. EVERY
  new creation should wire this too.
- **FIRST use of a new MCP tool / shape / parameter goes into a
  30-second scratch-scene probe** (fresh scene, one shape, one
  screenshot) before wiring it into a full creation run. Two full
  rockfall runs died on exactly this (capsule `radius=`,
  chamfer-on-deleted-ids); the probe costs ~5% of a run.
- **Wrap the build in `common.fail_soft`** (2026-08-09; creation 17 is
  the pattern): `with common.fail_soft(c, base_path, failed_glb=None):`
  around everything after the Creation is constructed - a crashed build
  still screenshots the aftermath (`<base>_failed.png`, optionally saves
  a `*_failed.glb`) before the process exits, instead of losing the
  whole run's evidence. It also restores the wall clock if the crash
  happened mid-manual-settle. EVERY new creation wires this.
- When done: commit the script (see Conventions), restore the ini (if
  headless was used), then run the script windowed with `--no-save` so
  the user can watch it build; the editor is left open.
- Outputs: screenshots `logs/creations/*.png`, headless-saved scenes
  `res/editor/scenes/creations/*.glb` (untracked; loadable with
  `load_scene`). Only the script is committed.
- `scripts/creations/common.py` is the shared API: scene bootstrap,
  look-at camera, material creation, graph builders, brushes, lights,
  physics helpers, screenshots, `group()` - plus module-level vector
  math (`v_add`/`v_cross`/`v_rotate`/...), `align_y_quaternion`,
  `probe_tilt` / `probe_pose` / `rest_rotation` / `body_axis_elevation`,
  `hierarchy_report`, and the randomized-placement kit promoted from
  creation 17 (`rand_quat` Shoemake, `tilt_yaw_quat`, `power_law_size`,
  `quantize_scale`, `sim(c, seconds)` manual-clock tick, `fail_soft`).
  IMPORT these, never re-derive them locally.
  Extend common.py rather than duplicating helpers - but keep
  per-creation code (L-system rules, critters) in the creation script.
- Runtime budget (measured 2026-08-08 against a live Release editor):
  queries ~10 ms, mutations ~15-25 ms, a posed shape (create + select +
  rotate + deselect) ~70 ms, get_scene_nodes on a 2400-node scene
  ~90 ms. Cost scales with CALL COUNT (a posed part is 4 calls), fixed
  sleeps and settle polls - not per-call latency. Every run prints an
  MCP telemetry summary at exit (per-tool counts + seconds); read it
  before optimizing anything. Background command timeout is 10 min;
  scenes of ~2500 nodes build fine.
- SETTLE SLEEPS dominate wall time, not MCP latency (2026-08-09,
  measured on creation 16: 411 -> 255 calls, get_async_status 60 -> 6).
  csg()/lattice_deform() ops on INDEPENDENT targets go out with
  wait=False + ONE settle() at the end of the builder; screenshot()'s
  settle is the backstop. presentation()'s hide pass runs once per
  scene automatically (was 147 set_window_visibility calls/run when it
  repeated per screenshot).
- `c.batch([{"tool": ..., "arguments": {...}}, ...])` (batch MCP tool,
  2026-08-08) runs N calls in one request, one editor frame and ONE
  undo entry - use it for burst construction (many create_shape /
  set_node_transform in a row) when telemetry shows call count
  dominating. Sub-call results come back in order; any sub-call error
  raises (earlier sub-calls stay applied). batch cannot nest and
  capture_screenshot cannot be inside one.
- Screenshot from 2-3 angles per iteration
  (`c.screenshot_views(base, [(suffix, eye, target), ...])`) -
  composition problems (occlusion, a buried face, a floating prop) then
  surface in ONE iteration instead of one per rerun. Object-relative
  cameras: `eye, target = c.shot_relative(node_name, local_eye,
  local_target)` converts OBJECT-LOCAL offsets through the node's live
  world transform - no hand heading trigonometry for close-ups.
- **Never GUESS a framing distance - fit it**: `eye, target =
  c.frame(node_name, azimuth, elevation, margin=1.3)` (2026-08-09)
  reads the subtree's merged world AABB (`get_node_details
  subtree_world_aabb`, one call) and places the camera where the
  bounding sphere fills the view from the given direction, returning
  the pair for screenshot_views. A guessed eye can land INSIDE the
  object (the rockfall `_cactus` shot did, twice); a fitted one cannot.
  `shot_relative` stays for authored close-ups.
- **Probe the ACTUAL surface instead of guessing offsets**
  (`geometry_query` MCP tool, 2026-08-09): `c.closest_points(points,
  node_name="Hull")` returns closest surface point + unit normal +
  distance per input point in ONE batched call;
  `c.geometry_query([...])` also does raycasts (same mask as the
  viewport hover ray). Convex hulls bulge past their authored station
  points - place hull-hugging trim at `hit + normal * standoff` rather
  than iterating hug factors from screenshots (that guessing was the
  single biggest time sink of the sail-ships bow sessions). Probe
  choice matters: closest_point answers "nearest surface anywhere" and
  DRIFTS toward the bulkier body near curved ends - for "surface AT
  this station" cast a RAY from outside toward the centerline
  (creation 16 hull_surface()).
  (creation 16 hull_surface()). The full hull-hugging band recipe is in
  `references/csg_hulls.md`.

## Scene-graph hierarchy (MANDATORY for all creations)

Every logical object (a plant, a robot, a prop, a craft) is ONE subtree
under its own group node, and nesting continues INSIDE the object
following its construction logic:

- `common.Creation.group(name, position, parent_node_id=None)` makes the
  empty root node. Children pass `parent_node_id=` on `create_shape` /
  `create_node` with **WORLD positions** - `create_shape` sets
  `world_from_node` first, then parents world-preservingly, and
  `create_node` inserts synchronously, so a just-created group/shape id
  is immediately usable as a parent. No reparent pass, no settle needed.
- **L-system structures mirror their bracket structure in the graph**:
  the turtle's bracket stack doubles as the node parent stack - each
  segment's parent is the segment it grew from (`[` pushes the current
  parent, `]` pops it); leaves/blooms/pinnae hang off their carrying
  segment; canopy clusters attach to the branch that contributed most
  tips.
- Non-L-system props nest by construction: mushroom group > stem > cap >
  speckles; moss on its rock; a fallen log carries its stubs, moss and
  colonizing mushrooms on the log node; a lamp's shade/bulb/light are
  children of the swinging rod.
- Children under rotated parents keep working with world positions and
  global-space `transform_selection` - creation 8 (lamp) and 10 (deep
  glade) rely on this.
- Verify with `get_scene_nodes` (`parent` / `parent_id` fields): count
  root-level nodes (should be ~one per object + camera/floor/lights) and
  the depth histogram. Reference: forest glade = 742 nodes, 37 roots,
  depth up to 7.

## Transform gotchas (cost real debugging time - trust these)

- **Pose at creation**: `create_shape` takes `rotation_xyzw`, `scale`
  (number = uniform brush bake, collision follows; [x,y,z] array = node
  TRS scale, visual only - pair with motion_mode "none") and `mass`
  (dynamic parts; inertia rescales). One call replaces the old create +
  select + rotate + deselect sequence, which was 69% of a big scene's
  MCP time.
- **Geometry reuse (2026-08-09)**: `common.shape()` pools by shape type
  + geometry parameters - repeated calls place instances of ONE
  content-library brush (`create_shape add_brush` once, `place_brush`
  after), so N same-shaped parts cost one geometry + GPU allocation and
  save/load as one glTF mesh with N node references. Free win: keep
  geometry parameters identical across repeated parts (quantize instead
  of jittering slice counts / radii); vary look per instance with
  material, node scale ([x,y,z]) and pose, which stay per-instance. A
  NUMBER bake scale builds one primitive per distinct value (memoized) -
  fine for a few sizes, wasteful for per-part random scales. Pass
  `reuse=False` if a placed instance must not register a library brush
  at all; geometry ops are safe on shared instances - they REPLACE the
  mesh's primitives, so the edited instance silently goes private while
  the others keep sharing. Brushes appear in the scene's Content
  Library (Brushes folder) and persist in the saved .glb via
  ERHE_brushes.
- **Unit-geometry parts**: for procedurally SIZED visual parts (jittered
  lengths/radii would defeat the pool), use `common.part()` - a unit
  box/sphere/cone brush shared per proportion key, sized per instance
  with node TRS scale (cone taper quantizes to 0.1 steps; it is baked
  geometry). Node scale scales the whole subtree, so `part()` keeps the
  scaled mesh a LEAF: pass `as_parent=True` when the part will carry
  children and it inserts an unscaled pose node as the attach point.
  NOT for physics parts - collision shapes and body shape="auto" hulls
  ignore node scale, so sway spines and static colliders keep `shape()`.
  The pose node is RIGID (position + rotation, no scale; server-side via
  place_brush pose_node), so joint carriers on pose nodes keep the same
  rotated constraint frames a mesh carrier would have.
- **Batch placements**: `batch = c.part_batch()` collects parts and
  `batch.flush()` places them all with ONE `place_brush_instances` call
  (one round trip, one editor frame). `batch.part()` returns a handle
  usable as the parent_node_id of LATER parts in the same batch
  (server-side parent_index) - whole branch chains batch; handles
  resolve to real node ids at flush. Only spines/statics interleave:
  create them with shape() (real ids), flush part batches around them.
  Pilot numbers (windswept glade, 2026-08-09): baseline 37.6 s wall /
  3060 MCP calls / 18.5 s MCP; unit-parts 33.0 s; + batching 25.8 s /
  1099 calls / 5.8 s MCP (~2900 placements in 66 batch calls, 0.4 s);
  create_shape 2231 -> 170; viewport frame ~9.6 -> ~8.2 ms.
- **`set_node_transform`** sets a node's transform by id/NAME with NO
  selection: ABSOLUTE semantics, all provided components in one call,
  undoable, rigid body teleported to the pose. It retries client-side
  ('Node not found') while a same-frame insert is pending - rotating a
  just-created light by name needs no settle() anymore. Prefer it over
  select_items + transform_selection everywhere (selection has side
  effects: gizmo rebind, selected dynamic bodies go kinematic).
- `transform_selection` (when you do use it) applies **ONE component
  per call** (translation OR rotation OR scale); combined calls
  silently drop components.
- Geometry ops (remesh / decimate / smooth / chamfer / merge_faces /
  catmull_clark) accept `node_ids` / `node_id` / `node_name` +
  `scene_name` since 2026-08-08 - no select_items dance; the previous
  selection is restored server-side.
- Align-to-direction quaternion (chained cones, blades): axis MUST be
  `cross(+Y, dir) = (d.z, 0, -d.x)`. The mirrored sign renders every
  chained segment tilted opposite its chain step -> gapped "dashed"
  trunks. Import `common.align_y_quaternion` (or
  `axis_angle_quaternion`); never re-derive by hand.
- Rotation pivots at the **node origin** (= cone base for base-origin
  cones). No bbox-center compensation.
- Non-uniform scale then rotation, as two sequential calls, composes as
  local TRS: elongated-Y sphere + align rotation = a blade pointing any
  direction (petals, pinnae, cut-face discs). UNROTATED anisotropic
  scale stays world-axis - orient every elongated part explicitly.
- Box `size` is plain world [x, y, z] extents (the old Create_box
  swap_xy quirk was fixed in the editor 2026-08-08; common.shape() no
  longer compensates - do not swap anywhere).
- Cone base sits at the node origin, +Y up, `height` upward; box and
  sphere are center-origin. Capsule is center-origin, length along Y.
- Debug recipe for placement bugs: build an ISOLATED minimal repro in
  the running editor (e.g. a 3-cone chain at 45 deg), then compare
  `get_scene_nodes` TRS against expectations, then screenshot.

## Materials & appearance

- `common.make_material(name, **fields)` CREATES a fresh material via
  the `create_material` MCP tool (2026-08-08) - no more 12-metal pool
  budget or copy_library_item fallback; make as many as the scene
  needs. Fresh materials are plain (no textures, isotropic BRDF,
  non-metal white), so `clear_textures` is a no-op kept for old call
  sites. Always set `metallic` and `base_color` explicitly - defaults
  are white non-metal, NOT the old stock-metal look.
- (Scene libraries still ship the 12 stock metals; they default to a
  circular-brushed anisotropic BRDF that draws an X/ring pattern per
  UV tile - avoid claiming them directly.)
- Translucency: raster transparency is selected by
  `blending_mode="alpha_blend"` + `opacity` (opacity alone renders
  OPAQUE; `transmission` only affects the ray tracer). Glass ~0.16
  opacity, water ~0.55-0.7 over a bright seabed, UFO shells ~0.30.
- Emissive: `emissive=[r,g,b]` with values ~2-3.5 for visible glow.
- Procedural texture graphs (`c.texture_graph` + fbm/noise/colorize ->
  output, `bind_material_texture`) give sand/granite-style surfaces;
  box-face UV tiling repeats per face at box default steps.

## Lighting

- Sky: `ambience(sky={"_version": 3, "enabled": True, "mode": 1})` for
  the physically-based blue atmosphere; `enabled` without `mode: 1`
  keeps the gray checker (mode 0 = gradient/checker), and without
  `_version: 3` the versionless JSON parses as v1 and drops fields.
  Indoor scenes: `sky=False` + dark `clear_color`.
- Point lights need intensity in the hundreds at room scale (150-400);
  they also wash large areas fast - accent glows want intensity < 100,
  range < 8 (a 320-intensity portal light turned a whole island pink).
- `cast_shadow=False` on fills avoids floor shadow blotches; keep ONE
  shadow-casting key light to ground objects.
- Directional sun: create, then rotate the node - light aims down -Z;
  pitch about X then yaw about Y (`quat_mul(qy, qx)`); pitch -134 deg ~
  45 deg elevation, -168 deg ~ dusk.
- Camera: `place_camera(eye, target)`; `exposure()` if needed. Check
  every screenshot for objects occluding the camera ray and for the
  floor's horizon edge (floor boxes want ~60 m for eye-level cameras).
- **Ground beyond the shadow fit renders as a hard dark band** (creation
  17): keep the WHOLE ground box - including its diagonal corners -
  inside `shadow_range` (500 m ground + 40 m range drew a giant dark
  trapezoid; a 160 m ground left a corner wedge past its 90 m range).
  Make the ground a THIN slab (0.08 m) so its unlit side face at the far
  edge reads as a hairline instead of a dark bar.
- Ground textures: a box face has ONE UV tile, so noise scales must be
  huge (fbm scale ~90 on a 110 m plane) or the pattern stretches to
  featureless flatness; and watch value-noise cell size - `noise` size 9
  = 12 m cells that read as a regular checker from a raised camera
  (size 48 keeps grain ~2 m).
- **Set up lights + `shadow_range()` FIRST** (before any geometry) so a
  windowed viewing is lit and shadowed from the first shape onward.
  `shadow_range(value)` also sets the camera far clip to `value` - for
  big open scenes pass `z_far` explicitly (e.g.
  `shadow_range(160, z_far=900)`) or the ground/sea plane far-clips
  into a hard horizon band.
  Light nodes insert on the NEXT frame (unlike create_node's synchronous
  insert); rotate the sun with `c.set_node_transform("<light name>",
  rotation_xyzw=q)` - its not-found retry rides out the pending insert,
  no settle() needed.
## Domain recipes (references/)

Stable domain knowledge lives in `references/` next to this file - read
the relevant file(s) BEFORE building in that domain, the same way this
file is read before any creation work:

- `references/vegetation.md` - L-system plants and trees
  (lsystem_trees.py species system), the one-spine physics LOD, sway
  motor rigs, sibling-spine collision filters, scene wind + tuning
  tables. Creations 9-10, 13, 15.
- `references/physics_rigs.md` - joint plumbing (anchors, limits,
  drives), toggle/wake semantics, ragdolls, load-bearing motor rigs
  (the standing spider) and pose probes. Creations 7-8, 14.
- `references/csg_hulls.md` - CSG carving (batched tools), authored
  convex-hull silhouettes, lattice/FFD deformation, bent-strip trim,
  probed hull-hugging bands, ship-scale composition. Creation 16.
- `references/settling_rock_piles.md` - the manual simulation clock
  (advance_time), deterministic settles, SCOPED wakes, staged
  construction (the cairn), rock piles, chamfer batches, dunes and
  cacti. Creation 17.
- `references/blades_sweep.md` - the sweep shape for blades / leaves
  (agave rosettes). Creation 17.

## Conventions

- Commit message: `scripts/creations: <scene> (<hook>)` + a body that
  records WHY and any debugging lesson; end with the Claude co-author
  line. Only the script (and common.py changes) are committed; the user
  pushes.
- prompt_queue.txt ITEM -1 and the `mcp-creation-scripts-*` agent
  memory only POINT at git log and this skill - do not grow commit
  ledgers in them; git log is the history.
- MCP node/material ids are per-session - never hardcode them.
- `select_items` requires `scene_name`. `place_brush` takes the full
  placement set since 2026-08-09 (rotation_xyzw, parent_node_id, name,
  scale number-or-array, mass, motion_mode incl. "none", brush_name) -
  identical to create_shape's instance parameters.

## Open bugs (workarounds in place; fix only if asked)

- (The chamfer crash is FIXED 2026-08-08: it was a cross-thread
  Operation_stack::queue in seven async mesh-op lambdas, not a
  uv_sphere geometry problem - chamfer/truncate/gyro/kis/merge_faces
  all work now.)
- (Both former blockers are FIXED 2026-08-08: `capture_screenshot`
  works windowed -- swapchain readback + one-frame MCP deferral -- and
  the windowed `save_scene` crash is gone: the glTF exporter saves only
  `Item_flags::content` children, so the hotbar rendertarget quad no
  longer reaches export.)
- Graphics preset High once shipped `shadow_light_count 32` (~2.1 GiB
  VRAM per view -> OOM with two scenes); trimmed to 8 locally in
  `config/editor/graphics_presets.json` - coordinate before reverting.
- **Geometry-graph mesh materials do not survive scene save/load**
  (found 2026-08-10 capturing the doc gallery): reloading
  `conway_cathedral.glb` renders every graph-mesh sculpture with the
  default white material (the graph re-evaluates, its output material
  binding is lost). Screenshots of graph-mesh creations must come from
  a fresh script build, not a loaded glb; fix the round-trip if asked.
