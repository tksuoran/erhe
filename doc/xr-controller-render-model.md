# XR controller render models (replacing the torus placeholder)

Research notes, 2026-08-13. Goal: show the real controller model in OpenXR
sessions instead of the torus placeholder created in
`src/editor/xr/controller_visualization.cpp`. Torus remains the fallback.

**Status 2026-08-13: IMPLEMENTED (untextured), Quest-3-verified.** The
XR_FB_render_model path is live: manifest declarations + extension enable +
session diagnostics, in-memory GLB parse, per-hand grip-pose nodes with the
runtime Touch Plus models, torus fallback retained. Remaining follow-up:
KTX2/BasisU texture transcoding (see "Gaps to close" item 3) - models
currently render with material factors only. Note learned on device: the
runtime demands BOTH the `com.oculus.feature.RENDER_MODEL` uses-feature AND
the `com.oculus.permission.RENDER_MODEL` uses-permission, and
xrGetRenderModelPropertiesFB fails with XR_ERROR_SESSION_NOT_RUNNING until
xrBeginSession.

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
