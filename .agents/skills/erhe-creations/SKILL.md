---
name: erhe-creations
description: Build an MCP-driven showcase scene ("creation") in the erhe editor. Read BEFORE starting, revising, or debugging any creation in scripts/creations/ - covers the workflow (self-launching scripts, headless screenshot iteration, windowed showing), the mandatory scene-graph hierarchy rules, and every hard-won MCP editor gotcha (transforms, materials, L-systems, physics, lighting). Revise this document at the end of each creation session with new learnings.
---

# Editor AI Creations

Showcase scenes built by driving the in-editor MCP server from Python
scripts in `scripts/creations/`. Each script is self-contained and
reproducible: it launches the editor, builds one scene, frames the
camera, screenshots (headless) and saves the scene.

**Maintenance contract:** read this document before any creation work;
at the end of a session, fold new facts into it (and prune anything it
made obsolete). This file is the canonical home for creation knowledge -
agent memory and prompt_queue.txt only point here.

## Existing creations

`creation_1_conway_cathedral` … `creation_14_spider_sentinel` (henge,
reef, robots, ragdoll, glass audience, sandbox + L-system oak, forest
glade, monster portal island, UAP hangar, windswept glade = glade +
physics foliage + wind, spider sentinel = motor-held STANDING ragdoll).
Look at the two or three most recent scripts before writing a new one -
they carry the current idioms.

## Workflow

- Every script self-launches the WINDOWED Release editor with
  `--commands config/editor/commands_empty.json` (no default scene; the
  creation scene is the ONLY scene). Flags from `common.standard_args`:
  `--reuse` (attach to running editor), `--editor-exe` (pick a build),
  `--pause N` (recording pause after first visible mesh, default 10 s),
  `--no-save` (REQUIRED for windowed runs - windowed `save_scene`
  crashes in glTF export; headless saves work).
- **Iterate with headless screenshots**:
  `py -3 scripts/creations/<script>.py --pause 0 --editor-exe
  build_vs2026_vulkan_headless/src/editor/Release/editor.exe`
  (`capture_screenshot` only works headless). Judge the PNG in
  `logs/creations/`, fix, rerun. Expect 2-4 iterations; composition
  problems (occlusion, framing, washed lighting) are the usual finds.
- Headless big viewports need
  `config/editor/desktop_window_imgui_host_imgui.ini` seeded with
  `[Window][Viewport_window N]` `Pos=8,28` `Size=2288,1160` for N=2..12
  BEFORE launch. **Back up the user's ini first and restore it after
  the headless iterations** (previous sessions keep a backup at
  `%TEMP%\erhe_imgui_ini_backup.ini`).
- When done: commit the script (see Conventions), restore the ini, then
  run the script windowed with `--no-save` so the user can watch it
  build; the editor is left open.
- Outputs: screenshots `logs/creations/*.png`, headless-saved scenes
  `res/editor/scenes/creations/*.glb` (untracked; loadable with
  `load_scene`). Only the script is committed.
- `scripts/creations/common.py` is the shared API: scene bootstrap,
  look-at camera, material pool, graph builders, brushes, lights,
  physics helpers, screenshots, `group()` - plus module-level vector
  math (`v_add`/`v_cross`/`v_rotate`/...), `align_y_quaternion`,
  `probe_tilt` / `probe_pose` / `rest_rotation` / `body_axis_elevation`,
  and `hierarchy_report`. IMPORT these, never re-derive them locally.
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
- Screenshot from 2-3 angles per iteration
  (`c.screenshot_views(base, [(suffix, eye, target), ...])`) -
  composition problems (occlusion, a buried face, a floating prop) then
  surface in ONE iteration instead of one per rerun.

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
- `create_box` applies `mat4_swap_xy`: world extents are
  `(size[1], size[0], size[2])`. `common.shape()` compensates - always
  go through it.
- Cone base sits at the node origin, +Y up, `height` upward; box and
  sphere are center-origin. Capsule is center-origin, length along Y.
- Debug recipe for placement bugs: build an ISOLATED minimal repro in
  the running editor (e.g. a 3-cone chain at 45 deg), then compare
  `get_scene_nodes` TRS against expectations, then screenshot.

## Materials & appearance

- Each new scene ships 12 stock metals; `common.make_material` claims
  and edits them in place - the pool is the hard budget (~12 materials
  per scene). Always set `metallic` explicitly.
- Stock metals default to circular-brushed anisotropic BRDF that draws
  an X/ring pattern per UV tile; `make_material(clear_textures=True)`
  resets textures + BRDF to plain isotropic. Use it for any flat color.
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
- **Set up lights + `shadow_range()` FIRST** (before any geometry) so a
  windowed viewing is lit and shadowed from the first shape onward.
  Light nodes insert on the NEXT frame (unlike create_node's synchronous
  insert); rotate the sun with `c.set_node_transform("<light name>",
  rotation_xyzw=q)` - its not-found retry rides out the pending insert,
  no settle() needed.
- Tree age variety (creation 13): pass an age/scale parameter driving
  size (~ age^0.7-0.8), L-system depth (+1 iteration when age >= 1.3,
  -1 for saplings), branch density, gnarl and canopy clump size; sway
  mass ~ age^2, receptivity ~ age (old trees lumber, saplings whip).

## L-system vegetation (creations 9-10 carry reference code)

- String rewrite with stochastic rules; interpret with a 3D turtle
  (heading/left vectors + Rodrigues rotation). Symbols: `F` segment,
  `[`/`]` push/pop, `&`/`^` pitch, `+`/`-` yaw or pinna side, `/` roll,
  terminal `A -> L`/`K` leaf/bloom.
- Trees: per-species parameter dicts (trunk, seg falloffs, pitch range,
  branch-count weights); leaf tips greedily clustered into canopy
  spheres (radius ~ sqrt(count)); one wobble pitch per `F` for gnarl.
- Ferns: rachis of ~6 short segments with progressive gravity droop and
  a pinna pair at nearly every node (~12 pinnae/frond reads realistic);
  basal pinnae expand a second L-system level (midrib + pinnule pair +
  tip). Flowers: stem segments + bracketed leaf blades + side-bloom
  stalks; blooms = center sphere + 5 aligned petal blades.
- 3-slice cones make triangles/prisms (TR-3B slab, portal ring); stand
  upright with `qx(90)`, then roll `qz(180)` for vertex-up.

## Physics (creations 7-8 carry reference code)

- Shapes with `motion_mode="dynamic"`; per joint create TWO coincident
  anchor child nodes (`common.anchor`, world positions), `settle()`,
  then `create_physics_joint node=anchorA connected=anchorB
  settings_name=<library settings>`. Limits: lock linear = all axes
  0..0; hinge = lock 2 angular + range on one; ball = range on all 3;
  weld = everything locked; pendulum-to-world = no connected node.
- `set_physics(enabled)` via toggle + verify (toggle only flips);
  bodies spawn DEACTIVATED - `wake_physics()` after enabling. Freeze an
  aftermath pose by toggling physics off before save.
- Pure-visual child parts: `strip_physics(node_id)`.
- `apply_physics_force` pokes a dynamic body (force / torque / impulse +
  optional world `point`). Impulses act immediately; forces last one
  fixed step (re-apply for a sustained push).
- `edit_physics_body` initial-velocity gotcha: `linear_velocity` only
  stores into the create info, and a shape edit in the SAME call
  recreates the body BEFORE it lands. Two calls: first
  `{linear_velocity, mass, ...}`, then `{shape, ...}` to trigger the
  recreation that applies it.

## Load-bearing motor rigs (creation 14 carries reference code)

Rest-pose motor drives are strong enough to act as MUSCLES, not just
sway springs: creation 14's 50-part ragdoll spider STANDS under full
gravity on motorized leg joints (and staggers + recovers from an
apply_physics_force shove).

- Explicit masses are essential for motor sizing, and `create_shape`
  cannot set mass while `edit_physics_body`'s mass edit does NOT rescale
  inertia. Create every part `motion_mode="none"`, pose it, then attach
  the body with `create_physics_body shape="auto" mass=<explicit>`
  (gravity_factor stays 1 - the point is to carry the weight).
- Size stiffness per joint from its static hold torque = (weight share
  at the contact point) x (horizontal lever from joint to contact), for
  ~0.02 rad of sag; graduate along the limb (spider hip 2400 -> toe 100
  Nm/rad). `max_force` ~5x hold torque makes a real shove yield the
  motors visibly before recovery. Drives on all 3 angular axes,
  position_target 0, limits ~+/-0.45 rad, linear locked.
- Feet/contact ends: place the authored pose so free-end tips just touch
  the floor (compute the drop analytically from the limb tables); the
  closed contact chain makes the stance much stiffer than open-chain
  spring sag suggests (measured: 24 mm sag on a 0.46 m stance).
- Pose probes: "tilt from world up" is useless for capsules rotated off
  +Y (reads 90 deg forever). Gate standing on height + drift from the
  REST rotation, and gate recovery on the body axis ELEVATION only
  (yaw-insensitive) - a shoved creature legitimately re-plants facing a
  new heading. A shove also slides it 1-2 m: re-frame the aftermath
  camera on the body's actual position, not the build position.

## Physics LOD for whole plants (creation 13 carries reference code)

For a scene full of swaying vegetation, do NOT chain bodies per
segment - give each plant ONE spine body and let the visual subtree
ride it:

- Visual parts (branches, canopies, pinnae, petals) are created with
  `motion_mode="none"` - NO rigid body at all. A static child body
  would grind against the dynamic spine and block the sway; "none"
  costs nothing and needs no strip pass.
- The spine node (tree trunk / frond rachis 0 / stem segment 0) gets
  `common.body(node_id, shape="auto", motion_mode="dynamic",
  mass=<explicit>, gravity_factor=0, wind_receptivity=...)` - "auto"
  hulls the node's own mesh, so the collision follows the visual.
- Coincident anchor child at the spine base + world joint with shared
  rest-pose motor settings. Collect (node, base, settings, mass,
  receptivity) jobs during the build and rig them AFTER settle() with
  the simulation still disabled.
- Lift plant bases a few cm (trees 0.05) so spine hulls clear the
  floor instead of grinding on it.
- `common.wind(...)` enables the scene wind (it carries the required
  `"_version": 2`); Creation accumulates scene settings, so wind and
  ambience no longer clobber each other - but call ambience/wind only
  through common, never raw set_scene_settings (it REPLACES the whole
  object).
- Verified per-scale tuning (mass / stiffness / damping / max_force /
  range / receptivity): tree 25 / 300 / 30 / 600 / 0.12 / 6.0,
  fern frond 0.08 / 6 / 0.5 / 8 / 0.35 / 0.3,
  flower stem 0.03 / 0.8 / 0.08 / 3 / 0.50 / 0.35, with wind speed 3,
  gusts 2.2 @ 0.4 Hz, turbulence 0.45, wavelength 9.
- Print a sway probe (sample tip tilt every 0.5 s): healthy plants
  OSCILLATE and recover; a monotonic tilt ramp that parks at the
  angular limit means receptivity is too high for the drive stiffness
  (iteration-1 ferns blew flat exactly this way).

## Bendy plants & wind (rest-pose motor joints; verified 2026-08-08)

Foliage that bends under impact / wind and springs back to its authored
pose needs no new machinery - six-dof drives ARE the rest-pose motor:

- **Rig**: chain of Y-axis capsules (`create_shape capsule` is
  center-origin), coincident anchor child pairs at each pivot as above;
  root anchor joints to the world (no connected node). Joint settings:
  lock linear XYZ (0..0) + angular Y (0..0), limit angular X/Z (about
  +/-0.9 rad), and add angular drives on axes 0 and 2 with
  `position_target 0`, `stiffness ~30`, `damping ~2`, finite
  `max_force ~60`. Target 0 = the pose at joint creation; the motor
  springs back to it, `max_force` is the yield threshold, the limits cap
  the bend.
- **Build with physics DISABLED, enable after the joints exist.**
  Segments free-fall for the frames between create_shape and the
  gravity_factor edit, and joints capture that fallen pose as the rest
  pose (a few degrees of permanent lean). Verified: disabled-build gives
  rest tilt exactly 0.
- `gravity_factor 0` on segments so the motor spring does not fight
  gravity (no droop below the authored pose); modest
  `angular_damping ~0.1`.
- Stiffness/max_force scale with segment thickness: stiff base, floppy
  tip reads plant-like.
- **Wind**: set `wind_receptivity` (kg/s) on segment bodies via
  create/edit_physics_body - increasing toward the tip (e.g. 0.7 base ->
  1.5 tip). Enable scene wind through `set_scene_settings` physics
  override; the object MUST carry `"_version": 2` or the wind fields are
  silently dropped by version migration (same trap as sky `_version`).
  Verified values: `wind_speed 6, wind_gust_amplitude 4,
  wind_gust_frequency 0.5, wind_turbulence 0.4, wind_wavelength 8` =
  dramatic 7-78 deg sway; quarter those receptivities for subtle
  ambient foliage. Wind force is
  `receptivity * (wind_velocity - body_velocity)` at the COM each fixed
  step; zero receptivity bodies are never touched (they sleep).
  `wind_receptivity` persists in the ERHE_physics extension on save.

## Conventions

- Commit message: `scripts/creations: <scene> (<hook>)` + a body that
  records WHY and any debugging lesson; end with the Claude co-author
  line. Only the script (and common.py changes) are committed; the user
  pushes.
- Update `prompt_queue.txt` ITEM -1's commit list and the
  `mcp-creation-scripts-*` agent memory pointer after each creation.
- MCP node/material ids are per-session - never hardcode them.
- `select_items` requires `scene_name`; `place_brush` has no rotation
  arg (rotate by returned `node_id`; names collide).

## Open bugs (workarounds in place; fix only if asked)

- `chamfer` op on a uv_sphere selection crashes the editor (use
  `remesh`).
- WINDOWED `save_scene` crashes during glTF export ("Rendertarget_mesh
  ... no retained source image bytes") - headless saves work; use
  `--no-save` windowed.
- `capture_screenshot` is headless-only (queue ITEM 4 tracks the
  windowed implementation).
- Graphics preset High once shipped `shadow_light_count 32` (~2.1 GiB
  VRAM per view -> OOM with two scenes); trimmed to 8 locally in
  `config/editor/graphics_presets.json` - coordinate before reverting.
