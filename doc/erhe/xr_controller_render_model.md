# XR controller render models

Stability: mostly stable

In an OpenXR session the editor shows the runtime's own controller models,
loaded through `XR_FB_render_model`. A torus placeholder built in
`src/editor/xr/controller_visualization.cpp` remains the fallback wherever the
extension is unavailable (desktop runtimes, older OS versions).

## Why `XR_FB_render_model`

`XR_EXT_interaction_render_model` + `XR_EXT_render_model` (the cross-vendor
pair, ratified June 2025) are the right long-term API, and erhe's vendored
OpenXR SDK already has the headers - but no runtime in reach implements them:
they appear neither in the Quest 3 runtime's enumerated extension list nor in
its "hidden pending manifest declaration" log lines, and Meta's PC Link
runtime lacks them too. `XR_FB_render_model` is what Quest supports for
controller models today. The erhe-side plumbing (GLB-from-memory -> scene
node) is written so the EXT pair can be slotted in behind the same abstraction
when runtimes ship it.

## What the extension gives

- A session-level API with no events:
  `xrEnumerateRenderModelPathsFB` -> `xrGetRenderModelPropertiesFB(path)` ->
  `xrLoadRenderModelFB(modelKey)`, which returns a GLB byte buffer.
- Controller paths `/model_fb/controller/left` and
  `/model_fb/controller/right`. **The model origin is at the grip pose**, so
  the render-model node follows `grip_pose`; `aim_pose` stays for ray and
  pointer logic.
- The properties call chains `XrRenderModelCapabilitiesRequestFB` on
  `XrRenderModelPropertiesFB::next`, declaring which support levels erhe can
  render. Both levels **require the glTF `KHR_texture_basisu` extension**
  (KTX2 / BasisU textures): SUBSET_1 is a single mesh, single texture, no
  transparency, unlit; SUBSET_2 allows multiple meshes and textures and
  texture transparency. erhe requests `SUBSET_2 | SUBSET_1`.
- `XR_RENDER_MODEL_UNAVAILABLE_FB` is a success code returned when the device
  is not connected yet; retry later. `modelKey` + `modelVersion` are stable
  across installs, so a model is cacheable.
- Loading may be slow, so it happens off the frame loop, like the editor's
  async imports.

## Requirements the runtime imposes

- **The Quest manifest must declare BOTH** the
  `com.oculus.feature.RENDER_MODEL` uses-feature AND the
  `com.oculus.permission.RENDER_MODEL` uses-permission. The runtime
  *implements* the extension but hides it from
  `xrEnumerateInstanceExtensionProperties` without the feature string, logging
  `skipping extension='XR_FB_render_model' due to: missing uses-feature
  string 'com.oculus.feature.RENDER_MODEL' from AndroidManifest`. A Quest
  manifest change needs a clean reinstall (uninstall first, not
  `install -r`), because the config is APK-bundled.
- `xrGetRenderModelPropertiesFB` fails with `XR_ERROR_SESSION_NOT_RUNNING`
  until `xrBeginSession`.
- The controller GLBs reference their KTX2 image only through
  `KHR_texture_basisu`'s extension-side `source`, which erhe's glTF parser
  resolves.

## What erhe does with the model

- **Extension enable** follows the ordinary pattern in
  `Xr_instance::create_instance` with `has_extension()` gating; the runtime's
  extension list is logged at instance creation.
- **GLB parse from memory**: the loaded byte buffer is parsed in place and
  built through the existing import / GPU finalize path.
- **KTX2 / BasisU textures** decode through the Basis Universal transcoder,
  transcoding to RGBA in `Image_loader`.
- **Per-hand grip-pose nodes** parent the resulting meshes; the torus fallback
  is retained and shown when no model is available.
- **Control bones are driven from input state.** The models are skinned with
  one joint per control (buttons, triggers, thumbstick), and the GLB carries
  one animation ("All Animations", 24 fps) whose specific frames hold each
  control's actuated pose. The frame layout, identical for both hands, is
  documented in `controller_visualization.cpp`. At load,
  `Controller_visualization::setup_control_drives()` samples the neutral and
  actuated pose per joint from that animation - input cannot be mapped to
  animation *time*, because the values step at the pose frames rather than
  ramp. Per frame, `update_hand_controls()` blends them by the mapped
  `Xr_actions` value (a/b/x/y click, trigger_value, squeeze_value) and sets
  the joint nodes' local transforms; the skinned-mesh joint-matrix path does
  the rest. The thumbstick blends four cardinal tilt poses (rotation vectors
  relative to neutral) by the stick vector. The oculus / menu button joint
  stays static, because it is not exposed to apps.

Extracted reference copies of the runtime GLBs live in
`res/editor/assets/Quest/`.

## The battery indicator quad

`<side>_batteryIndicatorQuad` is a 4-vertex quad (about 2 x 9 mm) on the top
face next to the thumbstick, skinned 100 % to the root joint, with its own
material `batteryIndicator_mat` and its own dedicated 256x64 KTX2 texture (the
main controller texture is the 512x512 one). That texture is a **four-cell
battery-level atlas**: four glowing discs, dim yellow (low) to bright white
(full). The quad's UVs as shipped span the full 0..1 atlas, and the consuming
app is expected to window them to one 64x64 cell by battery level - Horizon's
shell uses these meshes itself. Rendered naively, the quad shows all four
discs squashed together.

**Until a battery-level source exists the quad is HIDDEN at load**:
`Controller_visualization::load_render_model` skips nodes whose name ends with
`batteryIndicatorQuad` (visible flag cleared, no renderable mesh built). See
the plan for what a live indicator would need.

Tooling note: `basisu` (KTX2 / Basis decode to PNG) builds from the
`basis_universal` source CPM fetches into the cache; build it with CMake +
ninja from that source directory and run it with `-unpack -no_ktx`.

## Sources

- OpenXR specification sources: `ext_interaction_render_model.adoc`,
  `ext_render_model.adoc`, `fb_render_model.adoc`, and `registry/xr.xml`
  (subset flag comments) in an OpenXR-Docs checkout.
- [Meta: Render Controllers at Runtime (Unity / OVRPlugin - confirms controller model paths and KTX2)](https://developers.meta.com/horizon/documentation/unity/unity-runtime-controller/)
- [Khronos forum: Supported extensions by Meta (PC Link lacks EXT render model)](https://community.khronos.org/t/supported-extensions-by-meta/112150)
- [Khronos forum: Quest 2 controller rendering](https://community.khronos.org/t/quest-2-controller-rendering/110713)

## Future work

- [XR](../plans/xr.md) - the EXT render-model pair, a battery-level source for
  the indicator quad, and native compressed texture output.
