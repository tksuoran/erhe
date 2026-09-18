# XR: outstanding work

Status: proposed

Extends `doc/quest.md`, `doc/erhe/xr_controller_render_model.md`, `doc/editor/prewarm.md`
and `doc/erhe/multiview.md`.

## Controller render models

- **`XR_EXT_interaction_render_model` + `XR_EXT_render_model`** are the right
  long-term API (the cross-vendor pair, ratified June 2025), and the vendored
  OpenXR SDK already carries the headers, but no runtime in reach implements
  them: neither the Quest on-device runtime nor Meta's PC Link runtime
  enumerates them. Add them behind the same controller-model-provider seam
  `XR_FB_render_model` uses once runtimes ship it. The pair adds change
  events, per-node animation states and cross-vendor coverage; enumeration
  only returns models after the first `xrSyncActions`.
- **Battery indicator quad.** The quad is hidden at load because no controller
  battery level is available to third-party apps: the ratified
  `XR_EXT_interaction_profile_battery_state_display` is absent from the
  runtime's enumerated extensions and from its hidden-pending-manifest log
  lines, and `XR_FBX1_touch_controller_extras` is gated to first-party apps.
  Re-check after OS updates. Headset battery is available through Android
  `BatteryManager` (sticky `ACTION_BATTERY_CHANGED`, no permission) if showing
  headset charge on the controller is acceptable. Once a source exists, remap
  the quad's `TEXCOORD_0` from the full four-cell atlas to the level bucket's
  cell and rebuild the 4-vertex renderable when the bucket changes (rare,
  cheap).
- **Native compressed texture output.** The KTX2 / BasisU transcode produces
  RGBA today; transcoding straight to ASTC or BC7 would keep the controller
  textures compressed on the GPU.

## Prewarm

- **Pipeline-count reporting in the shadow log line.** It reports the node
  count, not the number of `vkCreateGraphicsPipelines` calls, which is
  `sum_over_nodes(render_passes.size())`.
- **Foveated / quad-view view counts.** The orchestrator sources view counts
  from `Xr_session::get_view_count()`. A Quest configuration that adds
  quad-view (4) or a foveated rendering path needs the prewarm to cover it.
- **Disabled composition passes are prewarmed** (one extra shader compile per
  unique `(key, view_count)`). Harmless today; gate it if a future build wires
  many disabled passes to feature flags.
- **Startup cost.** On Quest the prewarm is the largest single phase of
  startup. Reducing the variant set it walks is the lever.

## Multiview

- **View counts beyond stereo.** The infrastructure scales past two views but
  nothing exercises it; the capability gate requires exactly two views. A view
  configuration with more views (quad view) needs the gate widened and the
  per-eye fallback kept for the cases it still rejects.
