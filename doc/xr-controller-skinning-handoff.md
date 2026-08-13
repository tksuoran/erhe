# Handoff: drive controller render model bones from controller state

Goal for the next session: the XR controller render models are skinned
(one bone per control); pose those bones every frame from OpenXR input
state so buttons/triggers/thumbstick visually move in-headset.

## Where things stand (2026-08-13, all Quest-3-verified, unpushed on main)

Runtime controller render models are DONE as static models: fetched via
`XR_FB_render_model`, parsed from memory, textured (KTX2/Basis), skinned
vertex format, grip-pose per hand, torus fallback. Background + gotchas in
`doc/xr-controller-render-model.md`. Commits (oldest first): `d5ebef1e`
extension enable + diagnostics, `be539be8` in-memory GLB parse, `fcc14d95`
per-hand Controller_visualization, `b71c3deb` KTX2/Basis decode,
`12cbe93b` texture image index from KHR_texture_basisu, `80e7e082` doc,
`9e3dbd5f` bone visualizations honor per-view Skins mode (this fixed the
"spikes" - they were editor bone proxies, not a skinning bug), `7aab0547`
controller meshes build with the skinned vertex format.

## Key code

- `src/editor/xr/controller_visualization.cpp` - loads the GLB per hand in
  `load_render_model()` (called once per session from
  `Headset_view::update_actions`), builds meshes with
  `skinned_build_info` when `mesh->skin` is set, parents everything under
  the hand node (driven by grip pose in `update_hand()`).
- `src/erhe/xr/erhe_xr/xr_session.cpp` - `load_controller_render_model()`.
- `src/editor/xr/headset_view.cpp` ~line 2119 - the per-frame block that
  calls `update_hand()`; controller input state is in
  `m_headset->get_actions_left()/right()` (`erhe::xr::Xr_actions`,
  `src/erhe/xr/erhe_xr/xr_action.hpp`: booleans, floats, vector2f, poses).
- The parsed `Gltf_data` (nodes incl. joint nodes, skins, animations) is
  currently a local in `load_render_model()` and DROPPED after mesh build -
  the joint node shared_ptrs survive only through the scene graph
  (children of the hand node). To drive bones, keep what you need: store
  per hand the joint node pointers by name and the parsed animation(s).

## The asset (verified by inspection; extracted copy in res/editor/assets/Quest/)

`res/editor/assets/Quest/controller_right.gltf` + `_data.bin` + two .ktx2
(extracted from the runtime GLB; `controller_left.glb` / `controller_right.glb`
copies at repo root and in the scratch assets). Structure per hand:

- Node chain `root -> grip -> model -> skeleton #1` (grip carries
  T=(0,-0.02,-0.046), model carries a 180-degree Y rotation).
- Skin "skeleton #1", 7 joints, IBM accessor: `<side>_oculus_controller_world`
  (root joint, basis-change rotation approx. (-0.5,0.5,0.5,0.5)) with child
  joints `b_button_x`/`b_button_y` (left; `b_button_a`/`b_button_b` right),
  `b_button_oculus`, `b_trigger_front`, `b_trigger_grip`, `b_thumbstick`,
  plus non-joint `laser_begin`.
- Meshes `<side>_oculus_controller_mesh` (3529 verts) and
  `<side>_batteryIndicatorQuad` (4 verts), both skinned. JOINTS_0 is
  UNSIGNED_BYTE, WEIGHTS_0 float.
- **One animation "All Animations"** with 121-keyframe samplers of T+R per
  joint. This is almost certainly Meta's encoding of the control poses
  (like Unity's OVRRuntimeController / the XrControllers native sample):
  specific timeline positions hold each control's neutral / actuated pose.
  FIRST STEP of the design should be dumping the animation channels
  (times + values per joint) from the extracted glTF to learn the layout -
  e.g. which time index = button pressed, trigger fully pulled, stick
  deflection extremes. Then per frame: set each joint's local TRS by
  sampling/interpolating its channel with the mapped input value
  (button bool -> 0/1, trigger float -> 0..1, thumbstick vec2 -> blend of
  the four deflection poses), instead of hand-authoring transforms.

## Input mapping (erhe action names in Xr_actions)

- `b_trigger_front` <- trigger_value (float 0..1)
- `b_trigger_grip`  <- squeeze_value (float 0..1)
- `b_thumbstick`    <- thumbstick vector2 (x,y in -1..1) + thumbstick click
- `b_button_x/y` (left), `b_button_a/b` (right) <- a_click/b_click/x_click/y_click booleans
- `b_button_oculus` <- menu/oculus button (may not be exposed to apps; fine to leave static)
Check exact member names in `xr_action.hpp` / `xr_instance.cpp`
create-actions block (~line 1440).

## How erhe skins at render time

Joint matrices are computed from the skin's joint NODES' world transforms
x inverse bind matrices (see `erhe_scene/skin.hpp` skin_data +
`erhe_scene_renderer/joint_buffer.hpp`). So driving bones = setting the
joint nodes' local transforms (`Node::set_parent_from_node`) each frame;
no renderer work needed. The joint nodes already live under the hand node
in the scene. Set joint LOCAL transforms; the animation channels are also
local TRS, so sampled values drop straight in.

## Suggested plan

1. Offline: dump "All Animations" channels from the extracted glTF
   (python; there is `inspect_glb.py` precedent in the session scratchpad,
   or just parse the .gltf JSON + .bin) -> document time->pose layout per
   joint in this file.
2. `Controller_visualization`: retain per hand a `Joint_drive` table
   (joint node shared_ptr + its sampled channel keyframes, extracted from
   `gltf_data.animations` before it goes out of scope - erhe parses
   animations into `erhe::scene::Animation`; check its API for direct
   sampling, it may already interpolate).
3. New `update_hand_controls(Xr_actions*)` called from the same
   headset_view block: map inputs to channel times, sample, set joint
   local transforms. Cheap: 7 joints x TRS.
4. Quest verify: trigger pull, grip, thumbstick tilt, A/B/X/Y presses
   visibly animate. Desktop repro also possible: import the extracted
   glTF and drive joints via MCP `set_node_transform` to preview poses.

## Open follow-ups / warts

- `batteryIndicatorQuad` renders as a GIANT white ring (seen in desktop
  import; check in-headset). Likely meant to be hidden unless showing
  battery level. Consider hiding that mesh (by name) at load until
  understood.
- The GLB dump also showed `laser_begin` node (pointer origin hint) -
  could later replace the hand-authored ray origin.
- Push: main is many commits ahead of origin (controller work + earlier
  XR HUD work).

## Process reminders (hard-learned this session)

- READ `AGENTS.md` "Quest / Android device work" BEFORE any device work:
  install first, then prompt the user (AskUserQuestion Ready/Cancel), and
  only after confirmation run ONE `adb shell am start`; one confirmation
  = one launch. After a launch blocked by the controllers-required dialog,
  `adb shell am force-stop org.libsdl.app.quest` (and even then the dialog
  Continue can still start the app - warn the user).
- Quest logcat ring buffer holds ~5 s of erhe startup spam; `adb logcat -G
  16M` before capturing session-creation logs.
- Manifest/config changes need clean uninstall+install; code-only changes
  can use `install -r`.
- Desktop verify loop: windowed editor + its MCP server (127.0.0.1:8080)
  supports `capture_screenshot` now (not headless-only). create_scene ->
  import_gltf -> set_node_transform camera -> capture_screenshot. Kill
  editor before rebuilds; revert config/editor/*.json side effects before
  committing.
