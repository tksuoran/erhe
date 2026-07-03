# Cube-map point-light shadows

Status: **FIXED** (2026-06-22). The texture-coordinate bug was a per-face
**vertical (t-axis) flip**: the caster stored every cube face vertically
mirrored relative to what `samplerCubeArray` reads back, so point shadows were
displaced / mirrored. Root cause and fix are in
[Resolution](#resolution-2026-06-22-confirmed-vertical-face-flip-fixed) below;
the original design + debugging sections are kept for reference. The earlier
"partially working / mirrored in some regions" symptom is resolved.

Committed on branch `ls/main`:

- `22c1a50` docs: add `doc/forge-erhe.md` (SDL3 GPU -> erhe graphics API mapping)
- `a230415` editor: add cube-map point-light shadows

This document records the design, the per-file changes, the debugging plan, and
the known risks / tuning knobs.

## Resolution 2026-06-22: confirmed vertical face flip, fixed

**Root cause.** The point-light cube caster rendered each cube face through the
same pipeline as everything else, including erhe's Vulkan framebuffer y-flip,
which is done by a **negative-height viewport** in `set_viewport_rect`
(`vulkan_render_command_encoder.cpp:456`, VK_KHR_maintenance1) -- applied
uniformly to the screen pass AND the cube caster. But a cube face is not
displayed; it is read back by `samplerCubeArray` by direction, and the cube-map
face (s,t) convention is vertically inverted relative to the framebuffer row
order. So every stored face came out mirrored in t: geometry that the sampler
reads at (s,t) was stored at (s,1-t).

**How it was confirmed (RenderDoc fork capture, decisive).** Captured a frame,
`save_texture`'d the `-Y` layer of `Point shadow cube array` (slice 3) as DDS,
reconstructed the float distances, and for each shadow-casting solid (positions
from the in-editor MCP `get_node_details`) compared the stored distance at the
sampler's expected (s,t) against the vertically-mirrored (s,1-t). Result: 4/6
solids had their exact near-surface distance at (s,1-t) with **clear (1e30) at
(s,t)**; 0 matched (s,t). (The 2 "ambiguous" solids sat near the vertical centre
where (s,t)~=(s,1-t).) That is an unambiguous vertical face flip.

**The fix (convention-driven, the erhe-idiomatic form).** In `Shadow_renderer`'s
point-cube pass (`shadow_renderer.cpp`), the cube caster's `camera_buffer.update`
is given a `Coordinate_conventions` whose `clip_space_y_flip` is **enabled iff
`framebuffer_origin == top_left`** -- exactly the way the 2D shadow map derives
its flip from `framebuffer_origin` in `Light::get_texture_from_clip`. On a
top_left framebuffer (Vulkan/Metal) the projection y-negate cancels the
negative-viewport flip, so the cube face is stored in the GL-native orientation
that the fixed cube-map (s,t) convention expects; on bottom_left GL nothing is
flipped (its native rendering already matches). This is the single legitimate use
of `clip_space_y_flip` (disabled device-wide otherwise). No shader-side
`gl_Position` negate is used (an earlier iteration did `gl_Position.y = -...`,
but that is unconditional -- correct on Vulkan, wrong on bottom_left GL -- so it
was replaced with the framebuffer_origin-gated convention).

**Verified.** With the fix, point shadows render as proper contact shadows
directly under the occluders (before: detached / displaced into the foreground),
a ~47k-pixel change versus the pre-fix frame. The convention-driven form is
pixel-identical (0.01% AA jitter) to the shader-negate form on Vulkan.
Deductively it is the exact inverse of the confirmed flip and purely vertical
(s untouched). GL/Metal are unverified at runtime (no headless GL; the windowed
build needs a live display) but are correct by construction of the
framebuffer_origin gate.

### Note: why the FIRST `conventions` attempt was inert (the path was right, the value was wrong)

The original "leading hypothesis" (a per-face y-flip) was correct, and
`Coordinate_conventions` *is* the right vehicle -- but the first attempt passed
the wrong value:

- **`clip_space_y_flip` is `disabled` on every backend's DEVICE conventions.**
  Vulkan (`vulkan_device_init.cpp:1403`), OpenGL (`gl_device.cpp:1053`) and Metal
  (`metal_device.cpp:217`) all set
  `coordinate_conventions.clip_space_y_flip = disabled`; the per-backend
  framebuffer y-flip is done elsewhere (a negative-height viewport in
  `set_viewport_rect` on Vulkan, internal `(1-y)` on Metal, none on GL). So
  passing the **device** `parameters.conventions` (clip_space_y_flip already
  disabled) to the cube `camera_buffer.update` changes nothing -- the first
  attempt did exactly this and produced a **pixel-identical** capture. The fix
  above instead constructs a conventions value with `clip_space_y_flip` **enabled**
  for the cube pass (gated on `framebuffer_origin`), which is what actually flips
  the projection Y. Equivalent alternatives that were considered: a
  framebuffer_origin-gated shader `gl_Position.y` negate, or a **positive**-height
  viewport for the cube pass (do NOT inherit the screen's negative-viewport flip).
  All three are the same flip; the convention-driven form keeps it traceable to
  `framebuffer_origin` like the rest of erhe's coordinate handling.

### What was verified empirically (headless + in-editor MCP)

A new headless workflow was used (now documented in `CLAUDE.md` ->
"In-editor MCP server"): build `build_vs2026_vulkan_headless`
(`scripts/configure_vs2026_vulkan_headless.bat`), run the editor (it auto-runs
`config/editor/commands.json`, which now stands up a point-shadow test scene: one
shadow-casting point light + a platonic-solid cluster + floor), then drive the
editor's MCP server over HTTP (`POST http://127.0.0.1:8080/mcp`, JSON-RPC; note
the `id` field must be a **string**). `capture_screenshot` only works in this
headless build (emulated swapchain).

- Point shadows render; `VARIANT_SHADOW_CUBE` compiles; no errors in `logs/log.txt`.
- Moving the point light via MCP (`select_items` the light **node**, id 663 not
  the light item 664; `transform_selection` sets an **absolute** world
  translation) confirms lighting and shadows **track the light correctly** --
  lit faces and floor shadows follow the light. No **gross** mirror was observed
  at overhead, side, and elevated-side light positions.
- This neither confirms nor refutes the doc's "wrong in *some* regions" claim:
  a subtle per-face seam / orientation error is not legible from a shaded-floor
  screenshot under a single point light (the floor also dims with inverse-square
  falloff, so "shadow" vs "dim" is hard to separate; there is no MCP `edit_light`
  to boost intensity).

### How the faces were inspected (decisive, done)

The shaded-floor view was inconclusive (the gross shadow looked plausible), so
the cube faces were inspected directly via the **RenderDoc fork on the windowed
build** (`doc/renderdoc_fork.md`): `list_targets` -> `connect_to_target` ->
`trigger_capture`, then `save_texture` the `-Y` layer of `Point shadow cube
array` as **DDS** (PNG/EXR clamp the float distances; the `1e30` clear saturates
a default PNG). The DDS was reconstructed and the per-solid (s,t) vs (s,1-t) test
(above) proved the vertical flip. See [Resolution](#resolution-2026-06-22-confirmed-vertical-face-flip-fixed).
(The originally-scoped in-editor MCP "dump cube face" tool was not needed once
RenderDoc worked; it remains a viable headless alternative -- reach the texture
via `m_context.app_rendering->get_all_shadow_nodes()` + a `get_point_cube_texture()`
getter, and `Blit_command_encoder::copy_from_texture(tex -> buffer)` for readback.)

The original-design sections below remain accurate, including the
`conventions`/`clip_space_y_flip` framing of the y-flip: the final fix is
convention-driven (`cube_conventions.clip_space_y_flip` enabled iff
`framebuffer_origin == top_left`, set in `shadow_renderer.cpp`; no shader-side
`gl_Position.y` negate -- `standard.vert` documents this explicitly). An interim
fix did negate `gl_Position.y` in the caster vertex shader, but it was
superseded the same day by the convention-driven form.

## Overview

Ported the forge-gpu `forge-point-light-shadows` skill (SDL3 GPU Lesson 23) onto
erhe's shadow + forward renderers. Point lights now cast **omnidirectional**
shadows using an R32F `texture_cube_map_array` that stores the **raw radial
distance** from the light (one cube = 6 faces per shadow-casting point light).
The forward pass samples the cube by the fragment->light direction and compares
the stored nearest-occluder distance against this fragment's distance.

Directional / spot 2D shadow maps are deliberately untouched. The two systems run
in parallel; point lights are split out of the 2D path entirely.

### Why this shape

- erhe already had an R32F "distance" shadow technique, so storing linear radial
  distance (not hardware depth) is idiomatic and avoids the per-face non-linear
  depth comparison the skill warns about.
- The forward lighting loop already had a dedicated **point shadow-mapped prefix**
  (`standard.frag`) that previously did no shadow sampling -- a ready hook.
- erhe already supports cube arrays end to end (`Texture_type::texture_cube_map_array`,
  render-to-single-face via `Render_pass_attachment_descriptor::texture_layer`,
  `samplerCubeArray` via `set_sampled_image`; see `src/erhe/graphics/test/test_texture_cube.cpp`).

### Key decisions

- **Raw radial distance**, not normalized by far. R32F has ample range/precision
  for world distances, so neither the caster nor the receiver needs the far plane.
  Cube faces are cleared to a large value (`1e30`) so empty texels read as "lit".
- **No SDL3-style `proj.m[5]` negate.** Reverse-Z and the clip-space y-flip go
  through erhe's `Projection` + the caster pipeline centrally. The six face
  cameras use the standard GL cube up-vectors + `create_look_at`.
- **Separate dense index** for point shadows. A shadow-casting point light gets a
  `point_shadow_index` (cube-array layer base, written to `shadow_index_packed.y`)
  and its 2D `shadow_index` is left at `max()` so the 2D shadow loop / 2D receiver
  sampling skip it for free. Point is the last shadow-bearing type bucket, so this
  does not shift directional/spot 2D layer indices.

## Data flow / per-file changes

### Config (new settings, as requested)
- `src/editor/config/definitions/graphics_preset_entry.py` -- bumped struct to
  **v7**, added `point_shadow_resolution` (default 512) and
  `point_shadow_light_count` (default 2). Regenerated by the build (codegen
  `add_custom_command` DEPENDS on the `.py`).
- `src/editor/app_settings.cpp` -- clamps for the two new fields (resolution <=
  max_shadow_resolution and <= 4096; count <= 8; both >= sane minimums).
- `src/editor/windows/settings_window.cpp` -- two sliders ("Point Shadow
  Resolution", "Point Shadow Count").
- `config/editor/graphics_presets.json` + `graphics_presets_openxr.json` -- v7,
  added the two keys to each preset (Low 512/2, Medium 1024/4, High 2048/4,
  OpenXR 512/1).

### Texture + render-pass allocation
- `src/editor/rendergraph/shadow_render_node.{hpp,cpp}` -- new members
  `m_point_cube_texture` (R32F `texture_cube_map_array`, `array_layer_count =
  6*point_light_count`), `m_point_cube_depth` (shared 2D depth scratch reused for
  every face), `m_point_render_passes` (6*count passes; face f of cube p ->
  `[6*p+f]`), plus `m_point_resolution` / `m_point_light_count` / `m_point_viewport`.
  `reconfigure()` gained `point_resolution` + `point_light_count` (and they join
  the match-and-skip guard). Color faces clear to `1e30`; depth scratch clears to
  the reverse-Z value, store DONT_CARE.
- `src/editor/app_rendering.cpp` -- reads `point_shadow_resolution` /
  `point_shadow_light_count` from the preset and threads them into the
  Shadow_render_node ctor + `reconfigure()` (both the create and the
  settings-changed paths).

### Light indexing + GPU data
- `src/erhe/scene/erhe_scene/light.hpp` -- `Light_projection_transforms` gained
  `point_shadow_index`; declared `point_light_projection_transforms`.
- `src/erhe/scene/erhe_scene/light.cpp` -- point lights now route to
  `point_light_projection_transforms` (was: fall through to spot). It carries the
  light world pose + a 90-degree perspective with `z_far = range`; the six face
  view-projections are built in Shadow_renderer, not stored here.
- `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.{hpp,cpp}`:
  - `Light_projections::apply` -- parallel dense counter assigns
    `point_shadow_index` to shadow-casting point lights; their 2D `shadow_index`
    stays `max()`.
  - `Light_buffer::update` -- writes `point_shadow_index` to
    `shadow_index_packed.y`.
  - Light control block extended with `point_light_position` (vec4: xyz =
    light world pos, used by the caster); `update_control` gained the param.
  - New `m_fallback_point_cube_texture` (1x1 cube array, cleared to `1e30`);
    `bind_shadow_samplers` binds the real cube (from
    `Light_projections::shadow_cube_texture`) or the fallback.
  - `c_texture_heap_slot_shadow_cube = 3`; `Light_projections::shadow_cube_texture`.

### Caster (shadow pass)
- `src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp` -- new
  `VARIANT_SHADOW_CUBE` bool axis.
- `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.{hpp,cpp}`:
  - Extracted the per-light 2D bucket walk into
    `Shadow_renderer::draw_shadow_casters(...)`, now shared by both passes.
  - New point-cube loop after the 2D loop: for each shadow-casting point light
    with `point_shadow_index < cube_count`, render 6 faces. Each face builds a
    `create_look_at(light_pos, light_pos + cube_look[f], cube_up[f])` camera
    (Vulkan face order +X,-X,+Y,-Y,+Z,-Z) with the light's perspective
    projection, sets `update_control(..., vec4(light_pos, far))`, and draws with
    `cull_none` + `color_blend_disabled` (R32F write) under `VARIANT_SHADOW_CUBE`.
    Full-face scissor (no 1-texel border, so faces meet seamlessly).
  - `Render_parameters` gained `point_cube_texture`, `point_cube_render_passes`,
    `point_shadow_viewport`; render() assigns
    `light_projections.shadow_cube_texture = point_cube_texture`.

### Shaders
- `res/shaders/erhe_standard_variant.glsl` -- `VARIANT_SHADOW_CUBE` joins the
  POSITION_PASS gate (skips lit varyings) but a new `ERHE_USE_VARYING_POSITION`
  gate re-enables `v_position` for it.
- `res/shaders/standard.vert` -- declares `v_position` under
  `ERHE_USE_VARYING_POSITION`; assigns it for the cube variant.
- `res/shaders/standard.frag` -- under `VARIANT_SHADOW_CUBE`, outputs
  `length(v_position.xyz - light_control_block.point_light_position.xyz)` to the
  R32F face and returns (beside the existing distance-variant early-out).
- `res/shaders/erhe_light.glsl` -- `sample_point_light_visibility(world_pos,
  light_pos, cube_index)`: samples `s_shadow_cube` by direction, raw-distance
  compare with `bias = max(0.05, 0.02*current)`.
- `res/shaders/standard.frag` (point shadow-mapped prefix) -- multiplies the
  point light's intensity by `sample_point_light_visibility(..., float(light.shadow_index_packed.y))`.
- `program_interface.cpp` -- `s_shadow_cube` `sampler_cube_map_array` binding
  (color aspect, immutable no-compare sampler, fragment stage) in the scene bind
  group layout.

## Build / codegen

- Windows / Vulkan: `cmake --build build_vs2026_vulkan --target editor --config Debug`
  (VS 2026 generator; MSBuild locates the toolchain). The config codegen re-runs
  automatically because `erhe_codegen_generate`'s custom command DEPENDS on the
  `.py`. Verified: `graphics_preset_entry.hpp` regenerated with the two fields;
  full editor build links clean.

## Debugging the texture-coordinate bug (VS MCP + RenderDoc fork MCP)

The editor now runs fine on the desktop GPU. The earlier "could not run from a
detached shell" problem is gone -- **launch the editor through the Visual Studio
MCP server** (`debugger_launch`), which gives a real GPU session, and **capture +
inspect frames through the RenderDoc fork MCP server**. The full mechanics of
that setup (config, layer coexistence, connect_to_target, trigger_capture, the
inspection tools, gotchas) are documented once in
[`doc/renderdoc_fork.md`](renderdoc_fork.md) -- read that first; this section is
only the point-shadow-specific recipe on top of it.

### 0. Make sure a shadow-casting point light is in the scene

The cube path is dormant unless a `type = point`, `cast_shadow = true` light
exists (the default scene historically had only directional + spot). Either add
one through the editor UI (Add Lights), or temporarily force one in
`Scene_builder::add_lights` (`src/editor/scene/scene_builder.cpp`) after the spot
loop -- a `Light` with `type = point`, `cast_shadow = true`, `range ~= 30` above
some geometry. Revert that hack before committing. The first point-shadow frame
compiles the `VARIANT_SHADOW_CUBE` shader (one-time hitch). Put an **asymmetric**
occluder near the light (e.g. an L-shape or a single off-center box) -- a
symmetric scene hides exactly the mirror/rotation errors we are hunting.

### 1. Launch + capture

Follow [`doc/renderdoc_fork.md`](renderdoc_fork.md): start the fork
`qrenderdoc --mcp-server`, `debugger_launch` the editor via VS MCP, then
`list_targets` -> `connect_to_target` -> `trigger_capture open=true`.

### 2. What to inspect (point-shadow-specific resource / event labels)

- **The cube array texture** is labelled **`Point shadow cube array`** (R32F
  `texture_cube_map_array`, `6 * point_shadow_light_count` layers; layer
  `6*cube + face`). `list_textures` finds it by that label. It stores **raw
  radial distance** from the light; unrendered texels hold the `1e30` clear.
- **The caster passes** are labelled **`Point shadow cube <c> face <f>`**
  (`search_actions query="Point shadow cube"`), inside the
  `Shadow_renderer::render()` group, Vulkan face order `+X,-X,+Y,-Y,+Z,-Z`. Each
  is one render pass writing one cube face (`shadow_render_node.cpp:308-339`).
- **The receiver** is `sample_point_light_visibility` in
  `res/shaders/erhe_light.glsl:303`: it samples `s_shadow_cube` with
  `vec4(world_position - light_position, cube_index)` and does a raw-distance
  compare. The forward draw that calls it is the main viewport `standard.frag`
  pass (`standard.frag:438-450`).
- **The caster shader** writes `length(v_position.xyz - point_light_position)`
  to the R32F face under `VARIANT_SHADOW_CUBE` (`standard.frag:161`).

### 3. The leading hypothesis: per-face y-flip / cube-face orientation

The single most likely "incorrect texture-coordinate transformation" is a
**mismatch between how the caster orients each rendered cube face and how the
`samplerCubeArray` hardware expects that face to be oriented**. erhe applies a
central clip-space **y-flip** for Vulkan (correct for normal screen rendering),
but the Vulkan/GL **cube-map face coordinate system is fixed by the spec** and is
*not* screen-oriented. Rendering each face through the normal y-flipped pipeline
can therefore store each face **vertically mirrored** (or rotated, depending on
the face) relative to what the sampler reads back -- which looks exactly like
"partially working": correct where the occluder is symmetric about the flip axis,
wrong where it isn't, with errors strongest near face seams. The `cube_up` table
and the no-`proj.m[5]`-negate decision are in `shadow_renderer.cpp:480-489` and
flagged under [Known risks](#known-risks--tuning-knobs).

**Confirm or refute it with the capture (decisive, ~5 tools):**

1. `save_texture` each face layer of `Point shadow cube array` (slice
   `6*cube+face`, format `exr`/`hdr` to keep the float distance) and look at the
   stored distance image. With the asymmetric occluder, is the occluder silhouette
   in each face **oriented as the light would see it**, or vertically mirrored /
   rotated? `get_texture_stats` per slice first to find which faces were actually
   rendered (min far below `1e30`) vs left at clear.
2. Pick a fragment that is **wrongly** shadowed/lit. Compute its
   `light_to_frag = world_pos - light_pos`; the dominant axis is the face the
   sampler uses. `set_event` to that forward draw and `debug_pixel` it -- step
   `sample_point_light_visibility` and read the sampled `stored` vs `current`.
3. Cross-check: open the matching `Point shadow cube <c> face <f>` caster pass and
   `read_texture_region` / `pick_pixel` at the texel the sampler *should* hit. If
   the geometry is present in the face but at a **mirrored/rotated uv** vs where
   the sampler reads, that is the transform bug, and it localises whether the flip
   is on the store (caster) or the load (sampler direction) side.

**Candidate fixes once confirmed** (do not apply blind -- let the capture say
which): flip the per-face `cube_up` signs in `shadow_renderer.cpp:485-489`; or
suppress the central y-flip specifically for the cube caster pipeline; or negate
the sampled direction's y in `sample_point_light_visibility`. These are mutually
exclusive -- exactly one coordinate space is wrong; the capture tells you which.

### 4. Alternative suspects (if the faces look correctly oriented)

- **Face order / look vectors.** `cube_look` (`shadow_renderer.cpp:480-484`) must
  match the layer order the sampler selects. If shadows are wrong by a whole face
  (not mirrored within a face), the `+X,-X,+Y,-Y,+Z,-Z` look/up/layer mapping is
  off, not the in-face orientation.
- **Cube layer index.** The receiver passes `cube_index = shadow_index_packed.y`
  to the 4th `samplerCubeArray` coord; confirm it equals `6*point_shadow_index`
  vs `point_shadow_index` (array layer base, *not* face). A wrong base samples
  another light's cube. Check `light_buffer.cpp` (`point_shadow_index` ->
  `shadow_index_packed.y`) against the sampler's `index * 6 + face` expectation.
- **World-space vs view-space.** Caster stores `v_position - point_light_position`
  (world). Receiver uses `world_position - light_position` (world). Both must be
  world; verify `v_position` and the UBO `point_light_position` are in the same
  space in the capture's constant buffers.
- **Bias / range.** `bias = max(0.05, 0.02*current)` and `far = light->range`. A
  global over/under-shadow that is *not* position-dependent is bias/range, not a
  coordinate transform -- a different bug class.

### 5. Verify the fix the same way

Re-`build_project` via VS MCP, relaunch, recapture, and re-run step 3 -- the
wrongly-shadowed fragment should now sample the correct face/texel, and the saved
face images should be consistently oriented. Then run the regression pass below.

### 6. Regression (after the shadow looks correct)

1. Omnidirectional shadow tracks the light as it orbits; toggling `cast_shadow`
   makes it appear/disappear; a 2nd point light exercises
   `point_shadow_light_count`.
2. Directional + spot shadows look identical to before; a mixed
   dir+spot+point scene lights correctly (validates the index split).
3. Changing `point_shadow_resolution` / `point_shadow_light_count` in the preset
   reallocates the cube array (reconfigure match-and-skip respects them).
4. `grep -iE "error|fatal|No shader variant|No render pipeline" logs/log.txt`.

## Known risks / tuning knobs

- **Face orientation (y-flip).** Used GL cube up-vectors + erhe's central
  clip-space y-flip (no `proj.m[5]` negate). If shadows look mirrored near
  cube-face seams, flip the per-face `cube_up` signs in `shadow_renderer.cpp`.
- **Bias.** `erhe_light.glsl::sample_point_light_visibility` uses
  `max(0.05, 0.02*current)` world units. Too small -> acne; too large ->
  peter-panning. Tune per scene scale.
- **Shared depth scratch sync.** All 6*count faces reuse one 2D depth texture
  (cleared each pass, store DONT_CARE). Relies on erhe's render-pass barriers to
  serialize write-after-write across passes. If a Vulkan WAW hazard shows up
  (needs the validation layer, which is currently broken here), give each cube
  its own depth or add an explicit barrier.
- **Cap behavior.** If a scene has more shadow-casting point lights than
  `point_shadow_light_count`, the extra lights' `point_shadow_index` exceeds the
  array; the caster loop skips them and the sampler clamps the array layer (wrong
  shadow, not a crash) -- same implicit cap assumption as the 2D `shadow_light_count`
  path. Consider logging dropped point lights if this matters.
- **Memory.** R32F cube arrays are heavy (512^2 cube ~6 MB x 6 faces x count).
  Defaults (512 / 2) keep it ~12 MB; the High preset (2048 / 4) is ~400 MB --
  revisit if that is too much.

## Reference

- Skill: `/d/forge-gpu/.claude/skills/forge-point-light-shadows/SKILL.md`
  (Lesson 23). API mapping: `doc/forge-erhe.md`.
