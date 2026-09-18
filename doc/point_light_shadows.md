# Cube-map point-light shadows

Stability: stable

Point lights cast omnidirectional shadows through an R32F cube-map array that
stores the raw radial distance from the light. Directional and spot lights use
the 2D depth shadow map array described in [`shadows.md`](shadows.md); the two
systems run in parallel and point lights are split out of the 2D path entirely.
This document is the cube path's own design; `shadows.md` holds the summary and
the editor integration.

## Why this shape

- erhe already had an R32F "distance" shadow technique, so storing linear radial
  distance (not hardware depth) is idiomatic and avoids a per-face non-linear
  depth comparison.
- The forward lighting loop has a dedicated point shadow-mapped prefix in
  `standard.frag`, which is where the cube sample hooks in.
- Cube arrays are supported end to end: `Texture_type::texture_cube_map_array`,
  render-to-single-face via `Render_pass_attachment_descriptor::texture_layer`,
  and `samplerCubeArray` via `set_sampled_image` (see
  `src/erhe/graphics/test/test_texture_cube.cpp`).

## Key decisions

- **Raw radial distance**, not normalized by far. R32F has ample range and
  precision for world distances, so neither the caster nor the receiver needs
  the far plane. Cube faces clear to `1e30`, so empty texels read as "lit".
- **No projection-matrix y negate.** Reverse-Z and the clip-space y-flip go
  through erhe's `Projection` and the caster pipeline centrally. The six face
  cameras use the standard GL cube up-vectors plus `create_look_at`.
- **Separate dense index.** A shadow-casting point light gets a
  `point_shadow_index` (cube-array layer base, written to
  `shadow_index_packed.y`) and its 2D `shadow_index` stays `max()`, so the 2D
  shadow loop and 2D receiver sampling skip it for free. Point is the last
  shadow-bearing type bucket, so this does not shift directional / spot 2D layer
  indices.

## The per-face coordinate flip (standing rule)

A cube face is never displayed: it is read back by `samplerCubeArray` by
direction, through the fixed cube-map (s,t) convention, which is vertically
inverted relative to the framebuffer row order. The cube caster's clip-space y
must therefore be flipped opposite to the screen pass, or every stored face is
mirrored in t and the shadows land displaced from their occluders.

The flip is expressed through the coordinate conventions, the way
`Light::get_texture_from_clip` derives the 2D map's flip: in the point-cube pass
(`shadow_renderer.cpp`), the cube caster's `camera_buffer.update` is given a
`Coordinate_conventions` whose `clip_space_y_flip` is **enabled iff
`framebuffer_origin == top_left`**. On a top_left framebuffer (Vulkan, Metal)
the projection y-negate cancels the backend's own framebuffer flip (a
negative-height viewport on Vulkan), so the face is stored in the orientation
the cube-map convention expects; on bottom_left OpenGL nothing is flipped,
because its native rendering already matches. This is the single legitimate use
of `clip_space_y_flip`, which is `disabled` in every backend's device
conventions.

Two traps follow from that:

- Passing the **device** conventions to the cube pass changes nothing (they
  already carry `clip_space_y_flip = disabled`); the pass must construct a
  conventions value with the flip **enabled** under the `framebuffer_origin`
  gate.
- An unconditional shader-side `gl_Position.y` negate produces the same pixels
  on Vulkan but is wrong on bottom_left OpenGL. `standard.vert` documents that
  no shader-side negate is used.

## Storage and passes

- **Texture.** One R32F `texture_cube_map_array` labelled `Point shadow cube
  array`, `6 * point_shadow_light_count` layers, layer `6*cube + face`, Vulkan
  face order +X,-X,+Y,-Y,+Z,-Z. `point_shadow_resolution` and
  `point_shadow_light_count` are graphics-preset fields (defaults 512 and 2).
- **Caster.** `Shadow_renderer`'s point-cube pass renders six faces per
  shadow-casting point light. Each face builds a
  `create_look_at(light_pos, light_pos + cube_look[f], cube_up[f])` camera with
  the light's 90-degree perspective (`Light::point_light_projection_transforms`,
  `z_far = light->range`), sets `update_control(..., vec4(light_pos, far))` and
  draws with `cull_none` and color blending disabled under
  `VARIANT_SHADOW_CUBE`. The caster fragment writes
  `length(v_position.xyz - light_control_block.point_light_position.xyz)` to the
  R32F face. Scissor covers the full face, so faces meet seamlessly. A shared 2D
  depth scratch is reused for every face, for rasterization only (store
  `DONT_CARE`).
- **Receiver.** `sample_point_light_visibility()` (`res/shaders/erhe_light.glsl`)
  samples `s_shadow_cube` with `vec4(world_pos - light_pos, cube_index)` and
  compares the fragment's radial distance against the stored nearest-occluder
  distance with a world-space bias `max(0.05, 0.02 * current)`. 1.0 = lit;
  `standard.frag` multiplies the point light's contribution by it.
- **Fallback.** A 1x1 cube array cleared to `1e30` is bound when no real cube
  exists.

## Where the pieces live

| File | Contents |
|---|---|
| `src/editor/config/definitions/graphics_preset_entry.py` | `point_shadow_resolution`, `point_shadow_light_count` |
| `src/editor/app_settings.cpp` | clamps for both fields |
| `src/editor/windows/settings_window.cpp` | the two sliders |
| `config/editor/graphics_presets*.json` | per-preset values |
| `src/editor/rendergraph/shadow_render_node.{hpp,cpp}` | cube texture, shared depth scratch, `6*count` render passes (face `f` of cube `p` at `[6*p+f]`), `reconfigure()` on resolution / count |
| `src/editor/app_rendering.cpp` | threads the preset fields into the node |
| `src/erhe/scene/erhe_scene/light.{hpp,cpp}` | `point_light_projection_transforms`, `Light_projection_transforms::point_shadow_index` |
| `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.{hpp,cpp}` | dense `point_shadow_index` assignment, `shadow_index_packed.y`, `point_light_position` control field, `shadow_cube_texture` + fallback, `c_texture_heap_slot_shadow_cube = 3` |
| `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.{hpp,cpp}` | `draw_shadow_casters()` shared by both passes, the point-cube loop, `Render_parameters::point_cube_*` |
| `src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp` | `VARIANT_SHADOW_CUBE` |
| `res/shaders/erhe_standard_variant.glsl`, `standard.vert`, `standard.frag` | `ERHE_USE_VARYING_POSITION` for the cube variant, the caster write, the receiver multiply |
| `src/erhe/scene_renderer/erhe_scene_renderer/program_interface.cpp` | `s_shadow_cube` `sampler_cube_map_array` binding |

## Risks and tuning knobs

- **Bias.** `max(0.05, 0.02 * current)` world units: too small gives acne, too
  large gives peter-panning. Tune per scene scale.
- **Shared depth scratch.** All `6 * count` faces reuse one 2D depth texture
  (cleared per pass, store `DONT_CARE`), so the passes serialize on
  write-after-write through erhe's render-pass barriers. Give each cube its own
  depth, or add an explicit barrier, if a Vulkan hazard is ever reported.
- **Cap behaviour.** Shadow-casting point lights beyond
  `point_shadow_light_count` get a `point_shadow_index` past the array; the
  caster loop skips them and the sampler clamps the array layer (a wrong shadow,
  not a crash) - the same implicit cap as the 2D `shadow_light_count` path.
- **Memory.** R32F cube arrays are heavy: the 512 / 2 defaults cost about 12 MB,
  a 2048 / 4 preset about 400 MB.

## Verification

1. The omnidirectional shadow tracks the light as it orbits; toggling
   `cast_shadow` makes it appear and disappear; a second point light exercises
   `point_shadow_light_count`.
2. Directional and spot shadows are unchanged, and a mixed
   directional + spot + point scene lights correctly (this validates the index
   split).
3. Changing `point_shadow_resolution` or `point_shadow_light_count` in the
   preset reallocates the cube array.
4. `grep -iE "error|fatal|No shader variant|No render pipeline" logs/log.txt`.

Put an asymmetric occluder near the light when judging a face by eye: a
symmetric scene hides exactly the mirror and rotation errors this path can
produce. To read the stored faces directly, capture with the RenderDoc fork
([`renderdoc_fork.md`](renderdoc_fork.md)) and `save_texture` the layer as DDS -
PNG and EXR clamp the float distances and the `1e30` clear saturates them.

## Reference

API mapping to the SDL3 GPU lesson this path was ported from:
`doc/reference/forge_erhe.md`.

## Future work

- [plans/shadows.md](plans/shadows.md) - point-shadow culling and budget work.
