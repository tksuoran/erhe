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

- **Radial distance**, not normalized by far: each texel stores the distance
  from the light, along the texel's centre ray, of the nearest caster's plane
  ("Stored distance"). R32F has ample range and precision for world
  distances, so neither the caster nor the receiver needs the far plane. Cube
  faces clear to `1e30`, so empty texels read as "lit".
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
  `point_shadow_light_count` are graphics-preset fields (defaults 512 and 2;
  the resolution is at least 64).
- **Caster.** `Shadow_renderer`'s point-cube pass renders six faces per
  shadow-casting point light. Each face builds a
  `create_look_at(light_pos, light_pos + cube_look[f], cube_up[f])` camera with
  the light's 90-degree perspective (`Light::point_light_projection_transforms`,
  `perspective_z_far = light->range`), sets
  `update_control(..., vec4(light_pos, point_shadow_resolution))` and draws
  with `cull_none` and color blending disabled under `VARIANT_SHADOW_CUBE`.
  The caster fragment writes the distance of "Stored distance" to the R32F
  face. Scissor covers the full face, so faces meet seamlessly. A shared 2D
  depth scratch is reused for every face, for rasterization only (store
  `DONT_CARE`).
- **Receiver.** `sample_point_light_visibility()` (`res/shaders/erhe_light.glsl`)
  fetches the texel that contains the receiver direction at its centre and
  compares the receiver plane's distance on that texel's centre ray, moved
  toward the light by the bound of "Receiver bias", against the stored
  distance. 1.0 = lit; `standard.frag` multiplies the point light's
  contribution by it. The caller passes the receiver's geometric normal
  (`get_receiver_geometric_normal()`, as for `sample_light_visibility()`).
- **Resolution.** `point_shadow_resolution` is at least
  `erhe::scene_renderer::c_min_point_shadow_resolution` (64; the editor's
  `Graphics_settings::apply_limits()` and the Settings slider hold it); the
  bias derivation needs it ("Receiver bias").
- **Fallback.** A 1x1 cube array cleared to `1e30` is bound when no real cube
  exists.

## Stored distance

Both passes select cube texels the same way: `get_point_shadow_texel_centre()`
(`res/shaders/erhe_point_shadow.glsl`) takes the major axis of a direction
from the light as the face, scales the direction to that axis to get face
coordinates in [-1, 1], and returns the direction of the centre of the texel
that contains them (major component +-1). The texel centres are the same
symmetric grid on every face, whatever the face's (s, t) orientation and the
per-face flip, so no face table is involved; the centre lies half a texel from
every texel and face boundary, so a cube lookup in its direction selects
exactly that texel.

The caster fragment runs at its pixel centre, which lies on the texel centre
ray. It takes the plane of its primitive - the geometric normal
`cross(dFdxFine(p), dFdyFine(p))` of the interpolated world position `p`, and
`p` itself - and stores the farther of the plane's radial distance on the
centre ray, `|N . (p - L)| / |N . d_c|` (`L` the light position, `d_c` the
unit centre direction; the centre is `get_point_shadow_texel_centre(p - L)`,
the fragment's own pixel), and `length(p - L)`. Any point of the plane gives
the same plane distance, so
where the rasterizer puts the interpolated point does not reach the stored
value: the vertex snap and the barycentric precision move `p` within the
primitive's plane. The interpolated point itself has no usable bound:
near-clipped primitives of a large floor span tens of thousands of face
pixels, and on `head_on_floor` (Medium) their interpolated points lay up to
0.0064 texel from the pixel centre ray, a radial error of up to 0.009 texel
toward the face edges. The farther-of-two rule is for coverage, as for the 2D
distance technique (`shadows.md` "The distance technique"): a primitive whose
snapped coverage reaches a centre past its true edge would otherwise store its
plane's extension, which at a convex crease is nearer the light than the
neighbour face by the snap step times the primitive's own slope; `p` lies
inside the primitive, on the far side of the neighbour's plane. Taking the
farther value never raises a receiver's own stored surface above its
reference, since that is the plane distance. A primitive within `erhe_point_shadow_plane_cos_min`
(0.01) of edge-on to the centre ray, or with no plane at this footprint (the
derivatives' cross product is 0), stores `length(p - L)`: its plane is
ill-conditioned there, and no receiver inside the R1 grazing limit reads it
as its own surface ("Receiver bias", validity).

## Receiver bias

`sample_point_light_visibility()` compares, like the tap offsets of the 2D
maps (`shadows.md` "Tap offsets"), the receiver plane's value on the fetched
texel's centre ray with the stored value, and subtracts the bounds of the
error sources ("Minimum bias" there), restated for radial distances. The cube
lookup is one nearest fetch, a single compare with no filter.

- **Reference.** `d_c` is the texel centre of the receiver direction; the
  receiver plane (normal `N` from `get_receiver_geometric_normal()`, tilted to
  `|N . L| = 0.05` below the R1 grazing limit, as in "Receiver depth
  gradient") meets the centre ray at `r_c = |N . (P - L)| / |N . d_c|`. The
  reference is `min(current, r_c)`, `current = |P - L|`: one-sided, since a
  centre whose plane point is farther from the light already compares lit.
  For the receiver's own surface `r_c` is what the caster stored, in real
  arithmetic. The half-texel diagonal is at most `sqrt(2) / resolution` rad
  (at a face centre), below the grazing angle `asin(0.05)` for a resolution
  of at least 29, so the centre ray meets every plane R1 admits in front of
  the light. An undetermined plane (the rule of "Undetermined receiver
  plane") takes the smallest `r_c` over the planes through `P` within the
  grazing limit of head-on,
  `current / (d_r . d_c + tan(alpha_max) |d_r x d_c|)` (`d_r` the receiver
  direction, `tan(alpha_max) = 19.97`), and `|N . d_c| = 0.05` for the terms
  below.
- **Coverage snap.** The rasterizer snaps vertices to 1/256 of a pixel, so a
  primitive covers pixel centres up to that step (per face axis) past its
  true edge, and there the stored plane is that primitive's, extended. At a
  convex crease of the receiver's own mesh the extended neighbour is nearer
  the light by up to the step times the two planes' radial-distance slopes.
  The term covers the receiver's slope: `r_c (q / |q|^2 - N / (N . q))`, the
  derivative of the plane's radial distance with respect to the face
  coordinates at the centre `q`, summed over the two face axes, times
  `(1/256) (2 / resolution)`; an undetermined plane bounds each component by
  `r_c (1 + 1 / 0.05) / |q|`. It is the cube form of the 2D maps'
  `snap_bias`. Traced on `grazing_fan` (Medium, light 8 m above the fan):
  pixels on the 85 degree tile's large face, next to its edge with the 2 cm
  top face, read the top face's extended plane 0.04 mm nearer than their
  own; the term is about 0.6 mm there.
- **Gradient.** A normal error `dN` moves a plane's centre-ray distance by
  exactly `dN . (X - X_c) / (N' . d_c)`, `X` the point the plane is taken
  through and `X_c` its point on the centre ray. Receiver: the normal's error
  bound from `get_receiver_geometric_normal()` times the computed
  `|P - X_c|`, over `|N . d_c|` less the bound. Caster: its normal's error
  bound `e (2a + e) / a^2` (`e = 2 sqrt(3) gamma_4 |P|`, the error of one
  world-position derivative with `|P|` standing in for the caster's
  magnitudes, and `a = 2 r_c / (resolution |q|^2)` the smallest world size of
  a cube pixel at the centre, which maximizes the bound), times half the
  texel diagonal on the plane, `sqrt(2) r_c / (resolution |q| |N . d_c|)`,
  over `|N . d_c|` less the bound but at least the caster's plane threshold
  0.01.
- **Position.** The rounding of the receiver point and of the caster's
  interpolated point, `sqrt(3) gamma_4 |P|` each along the plane normal,
  moves each plane's centre-ray distance by that over `|N . d_c|`.
- **Evaluation.** Each side's `|N . v| / |N . d_c|`: `v` and `d_c` rounded,
  two three-term dots and the divide, `12u / |N . d_c| + 5u` relative to
  `r_c`; the receiver's `length()` when `current` is the reference, `13u`
  (the subtraction `u`, the dot `1.5u`, and `sqrt`, inherited from
  `inversesqrt` and a divide in Vulkan's precision rules, 4.5 ulps = 9u,
  rounded up).
- **Not terms.** R32F stores the caster's fp32 value exactly; both passes
  read the same fp32 light position (`world_from_light_camera` applied to the
  origin, in `Light_buffer` and in `Shadow_renderer`); both select the same
  texel centre, computed by the same expression from the same resolution.

The bias is

    snap
      + shadow_bias_texel_scale * (receiver gradient + caster gradient)
      + shadow_bias_origin_scale * (position + evaluation)

subtracted from the reference, with the two graphics-preset scales of the 2D
maps (`light_block.shadow_bias_scales`, default 1 = the derived bound) in
the same roles; the snap term is unscaled, like `snap_bias`. There is no
constant world floor. On `head_on_floor` (Medium, 1024 texels) the floor's
stored and reference distances differ by at most 2 % of the gradient,
position and evaluation terms, which add up to 0.0014 to 0.0077 texel there
(a texel is `2 r / resolution` at a face centre).

Validity. The tie is covered where the caster stored its plane. A receiver
inside the grazing limit meets its centre ray at `|N . d_c|` of at least
`0.05 - sqrt(2) / resolution` (0.028 at the minimum resolution 64), which
exceeds the caster threshold 0.01 as long as the two normal errors stay
below 0.018 rad together - a cube pixel of at least about `100 e` wide. A
crease neighbour steeper than the receiver is covered by the caster's
farther-of-two rule ("Stored distance"), not by a receiver term: with the
plane distance alone, on `grazing_fan` at 512 texels (Low, and the 512
pairwise configs at rasterizer slope 0) 6 pixels of the 15 degree tile's top
face, within a texel of its edge with the side face that faces away from the
light (`cull_none` stores it), read the side face's extended plane 0.18 mm
nearer than their own, against 0.08 mm of bias.

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
| `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.{hpp,cpp}` | dense `point_shadow_index` assignment, `shadow_index_packed.y`, `point_light_position` control field (xyz light position, w face resolution), `shadow_cube_texture` + fallback, `c_texture_heap_slot_shadow_cube = 3`, `c_min_point_shadow_resolution` |
| `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.{hpp,cpp}` | `draw_shadow_casters()` shared by both passes, the point-cube loop, `Render_parameters::point_cube_*` |
| `src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp` | `VARIANT_SHADOW_CUBE` |
| `res/shaders/erhe_standard_variant.glsl`, `standard.vert`, `standard.frag` | `ERHE_USE_VARYING_POSITION` for the cube variant, the caster write, the receiver multiply |
| `res/shaders/erhe_point_shadow.glsl` | `get_point_shadow_texel_centre()` and `erhe_point_shadow_plane_cos_min`, shared by the caster and the receiver |
| `res/shaders/erhe_light.glsl` | `sample_point_light_visibility()` |
| `src/erhe/scene_renderer/erhe_scene_renderer/program_interface.cpp` | `s_shadow_cube` `sampler_cube_map_array` binding |

## Risks and tuning knobs

- **Bias.** Derived, with no scene-scale constant ("Receiver bias"); the
  preset scales `shadow_bias_texel_scale` / `shadow_bias_origin_scale` apply.
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
([`renderdoc_fork.md`](../agents/renderdoc_fork.md)) and `save_texture` the layer as DDS -
PNG and EXR clamp the float distances and the `1e30` clear saturates them.

## Reference

API mapping to the SDL3 GPU lesson this path was ported from:
`doc/reference/forge_erhe.md`.

## Future work

- [plans/shadows.md](../plans/shadows.md) - point-shadow culling and budget work.
