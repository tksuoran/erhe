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

`creation_1_conway_cathedral` … `creation_12_uap_hangar` (henge, reef,
robots, ragdoll, glass audience, sandbox + L-system oak, forest glade,
monster portal island, UAP hangar). Look at the two or three most recent
scripts before writing a new one - they carry the current idioms.

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
  physics helpers, screenshots, `group()`. Extend it rather than
  duplicating helpers - but keep per-creation code (L-systems, critters)
  in the creation script.
- Runtime budget: ~0.3-0.5 s per MCP call, background command timeout
  is 10 min. A create_shape + scale + rotate is 4+ calls. Keep scenes
  under ~800 nodes / ~2000 calls or split the build.

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

- `transform_selection` applies **ONE component per call** (translation
  OR rotation OR scale); combined calls silently drop components.
  `common.move_node_id` handles this.
- Align-to-direction quaternion (chained cones, blades): axis MUST be
  `cross(+Y, dir) = (d.z, 0, -d.x)`. The mirrored sign renders every
  chained segment tilted opposite its chain step -> gapped "dashed"
  trunks. Use `common`'s `axis_angle_quaternion` or a verified local
  `align_y_quaternion`; don't re-derive by hand.
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
