# XR controller render models (replacing the torus placeholder)

Research notes, 2026-08-13. Goal: show the real controller model in OpenXR
sessions instead of the torus placeholder created in
`src/editor/xr/controller_visualization.cpp`. Torus remains the fallback.

**Status 2026-08-13: IMPLEMENTED INCLUDING TEXTURES, Quest-3-verified.**
The XR_FB_render_model path is live: manifest declarations + extension
enable + session diagnostics, in-memory GLB parse, per-hand grip-pose nodes
with the runtime Touch Plus models, torus fallback retained. KTX2/BasisU
textures decode through the Basis Universal transcoder (basis_universal
v2_50, transcode-to-RGBA in Image_loader; native ASTC/BC7 output remains
possible future work). Notes learned on device: the runtime demands BOTH
the `com.oculus.feature.RENDER_MODEL` uses-feature AND the
`com.oculus.permission.RENDER_MODEL` uses-permission;
xrGetRenderModelPropertiesFB fails with XR_ERROR_SESSION_NOT_RUNNING until
xrBeginSession; and the controller GLBs reference their KTX2 image only
through KHR_texture_basisu's extension-side `source`, which the glTF
parser now resolves.

**Status update 2026-08-13 (later session): control bones are DRIVEN from
input state, Quest-3-verified.** The models are skinned with one joint per
control (buttons, triggers, thumbstick), and the GLB carries one animation
("All Animations", 24 fps) whose specific frames hold each control's
actuated pose — the frame layout, identical for both hands, is documented
in `controller_visualization.cpp`. At load,
`Controller_visualization::setup_control_drives()` samples the neutral and
actuated pose per joint from that animation (input cannot be mapped to
animation *time* because the values step at the pose frames rather than
ramp); per frame, `update_hand_controls()` blends them by the mapped
`Xr_actions` value (a/b/x/y click, trigger_value, squeeze_value) and sets
the joint nodes' local transforms — the skinned-mesh joint-matrix path
does the rest. The thumbstick blends four cardinal tilt poses (rotation
vectors relative to neutral) by the stick vector. The oculus/menu button
joint stays static (not exposed to apps). Extracted reference copies of
the runtime GLBs live in `res/editor/assets/Quest/`.

## Battery indicator quad (investigated 2026-08-13, RESOLVED 2026-08-14: hidden)

`<side>_batteryIndicatorQuad` is a 4-vertex quad (~2 x 9 mm) on the top
face next to the thumbstick, skinned 100% to the root joint, with its own
material `batteryIndicator_mat` and its own dedicated 256x64 KTX2 texture
(`controller_right_img0.ktx2` in the extracted asset; the main controller
texture is the 512x512 one). The texture is a FOUR-CELL battery-level
atlas: four glowing discs, dim yellow (low) to bright white (full). The
quad's UVs as shipped span the full 0..1 atlas; the consuming app is
expected to window them to one 64x64 cell by battery level (Horizon's
shell uses these meshes itself - `HsrControllerMeshInfo` logcat lines).
Rendered naively it shows all four discs squashed onto the quad - the
current small bright blob on the controller.

Can it be LIVE? Controller battery is NOT available to third-party apps
on Quest today: the ratified `XR_EXT_interaction_profile_battery_state_display`
(chains `XrBatteryStateDisplayEXT` with a `batteryLevel` float onto
`xrGetCurrentInteractionProfile`; present in our vendored registry) is
absent from the runtime's 77 enumerated extensions and from its
hidden-pending-manifest log lines (verified in launch capture
2026-08-13). `XR_FBX1_touch_controller_extras` exists but is gated to
first-party apps (`isFirstPartyApp=false` -> skipped). Re-check after OS
updates. Headset battery IS available now via Android `BatteryManager`
(sticky `ACTION_BATTERY_CHANGED`, no permission), if showing headset
charge on the controller is acceptable UX.

Display mechanics once a data source exists: remap the quad's
`TEXCOORD_0` from full-atlas to the level bucket's cell and rebuild the
4-vertex renderable when the bucket changes (rare, cheap). Until a data
source exists the quad is HIDDEN at load (Quest-verified 2026-08-14):
`Controller_visualization::load_render_model` skips nodes whose name ends
with `batteryIndicatorQuad` (visible flag cleared, no renderable mesh
built). Revisit if the runtime ever exposes
`XR_EXT_interaction_profile_battery_state_display`.

Tooling note: `basisu.exe` (KTX2/Basis decode to PNG) is built at
`.cpm_cache/basis_universal/<hash>/bin/basisu.exe` (built 2026-08-13 with
`-unpack -no_ktx`; rebuild via CMake+ninja from that source dir if gone).

## Verdict

- `XR_EXT_interaction_render_model` + `XR_EXT_render_model` (the new
  cross-vendor pair, ratified June 2025) are the *right long-term API*, and
  our vendored OpenXR SDK 1.1.59 already has the headers — but **the Quest
  on-device runtime does not implement them** (verified empirically on the
  Quest 3, Horizon OS current as of 2026-08: the extensions appear neither in
  the enumerated list nor in the runtime's "hidden pending manifest
  declaration" log lines). Meta's PC Link runtime also lacks them (Khronos
  forum, Nov 2025).
- **`XR_FB_render_model` is what Quest actually supports for controller
  models today.** The runtime *implements* it but **hides it from
  `xrEnumerateInstanceExtensionProperties` unless the Android manifest
  declares `<uses-feature android:name="com.oculus.feature.RENDER_MODEL"/>`**.
  Verified on-device; the runtime logs:
  `skipping extension='XR_FB_render_model' due to: missing uses-feature
  string 'com.oculus.feature.RENDER_MODEL' from AndroidManifest`.

Recommendation: implement `XR_FB_render_model` now (Quest path), and design
the erhe-side plumbing (GLB-from-memory → scene node) so the EXT pair can be
slotted in later behind the same abstraction when runtimes ship it. Keep the
torus when neither extension is available (desktop runtimes, older OS).

## XR_FB_render_model facts (spec rev 4)

- Session-level API, no events: `xrEnumerateRenderModelPathsFB` →
  `xrGetRenderModelPropertiesFB(path)` → `xrLoadRenderModelFB(modelKey)`
  returns a GLB byte buffer.
- Controller paths: `/model_fb/controller/left`, `/model_fb/controller/right`
  — **model origin is at the grip pose** (the torus currently follows
  `aim_pose`; the render model node must follow `grip_pose`).
- Properties call: chain `XrRenderModelCapabilitiesRequestFB` on
  `XrRenderModelPropertiesFB::next` declaring which support levels we can
  render. Both levels **require the glTF `KHR_texture_basisu` extension**
  (KTX2/BasisU textures):
  - SUBSET_1: single mesh, single texture, no transparency, unlit.
  - SUBSET_2: multiple meshes/textures, texture transparency.
- `XR_RENDER_MODEL_UNAVAILABLE_FB` success code when the device is not
  connected yet; retry later. `modelKey`+`modelVersion` are stable across
  installs → cacheable.
- Load may be slow → do it off the frame loop (background thread), same as
  editor async imports.
- No node-animation API (that is the EXT pair's feature); FB models are
  static GLBs. Static real controller >> torus, so fine.

## What erhe already has / lacks

Already in place:
- Extension enable pattern: `Xr_instance::create_instance`
  (`src/erhe/xr/erhe_xr/xr_instance.cpp:331-564`), `has_extension()` gating;
  runtime extension list logged at `xr_instance.cpp:922`.
- Torus placeholder + node attach: `controller_visualization.cpp:41-77`;
  instantiated in `headset_view.cpp` (`attach_to_scene`), per-frame pose
  update near `headset_view.cpp:2119`. Note existing `// TODO both
  controllers` — only one torus exists today; render-model work naturally
  makes it two (left + right).
- glTF: `erhe_gltf` on fastgltf already accepts `KHR_texture_basisu` and
  recognizes KTX2 buffers; editor has full import→GPU finalize path
  (`src/editor/parsers/gltf.*`, `finalize_imported_meshes()`).

Gaps to close:
1. **Manifest**: add `<uses-feature
   android:name="com.oculus.feature.RENDER_MODEL" android:required="false"/>`
   to the Quest flavor manifest. Quest config/manifest changes need a clean
   reinstall (uninstall first, not `install -r`).
2. **GLB parse from memory**: `Gltf_parse_arguments` takes a filesystem path
   only. Either add a byte-span entry point (fastgltf supports memory
   buffers) or write the GLB to app cache dir (also gives us the
   modelKey/version cache for free).
3. **KTX2/BasisU decode**: image decoding is wuffs-based
   (`image_loader_wuffs.cpp`) — no KTX2. Both FB subsets require
   KHR_texture_basisu, so integrate the basis_universal transcoder (or
   libktx) to transcode to an uncompressed or GPU-compressed format at load.
   This is the single biggest new dependency.
4. **Grip-pose node**: controller visualization must anchor at grip pose for
   the render model (aim pose stays for ray/pointer logic).

## Suggested implementation order

1. Manifest feature + enable `XR_FB_render_model` in `Xr_instance` (gated by
   `has_extension`), log paths/properties on Quest. Cheap, proves the
   runtime serves models. (Request SUBSET_2 | SUBSET_1.)
2. KTX2 transcode support in image loading (basis_universal).
3. Memory GLB entry point in `erhe_gltf` + background load → build meshes via
   the existing finalize path; parent under per-controller grip-pose nodes;
   hide/show vs torus fallback.
4. Later, when runtimes ship it: `XR_EXT_interaction_render_model` backend
   behind the same "controller model provider" seam (it adds change events,
   per-node animation states, and works cross-vendor; enumeration only
   returns models after first `xrSyncActions`).

## Sources

- Local spec: `D:/OpenXR-Docs/specification/sources/chapters/extensions/ext/ext_interaction_render_model.adoc`, `ext/ext_render_model.adoc`, `fb/fb_render_model.adoc`; `registry/xr.xml` (subset flag comments).
- Device evidence: Quest 3 logcat, erhe launch 2026-08-13 (extension hidden-by-manifest log; EXT pair absent).
- [Meta: Render Controllers at Runtime (Unity/OVRPlugin — confirms controller model paths + KTX2)](https://developers.meta.com/horizon/documentation/unity/unity-runtime-controller/)
- [Khronos forum: Supported extensions by Meta (PC Link lacks EXT render model, Nov 2025)](https://community.khronos.org/t/supported-extensions-by-meta/112150)
- [Khronos forum: Quest 2 controller rendering](https://community.khronos.org/t/quest-2-controller-rendering/110713)
