# Detail blend weight: design

Status: in progress

The design (BD-numbers) for `doc/plans/detail_blend_weight.md` (B-numbers).
The build order is `doc/plans/detail_blend_weight_implementation.md`. The
design follows the proposed choices of the requirements; a choice the
requirements leave to the design is marked "choice" and listed in the
implementation plan's "Open" for the user's review.

## BD1 Material values

- BD1.1 `erhe::primitive::Material` gains these registered properties, in
  a property group "Detail Blend", with `Material_values` fields of the
  same names:

  | Property | Type | Default | Requirement |
  |----------|------|---------|-------------|
  | `detail_blend` | `bool` | false | B2.1 |
  | `detail_blend_weight_channel` | `Texture_channel` (`r`, `g`, `b`, `a`) | `r` | B1.1 |
  | `detail_blend_height_channel` | `Texture_channel` (`r`, `g`, `b`, `a`) | `r` | B2.2 |
  | `detail_blend_height_influence` | `float`, 0 - 1 | 1 | B2.3 |
  | `detail_blend_transition_width` | `float`, 0 - 1 | 0.2 | B2.3 |
  | `detail_blend_noise_amount` | `float`, 0 - 1 | 0 | B2.3 |
  | `detail_blend_noise_scale` | `float`, cycles per meter | 1 | B2.3 |
  | `detail_blend_noise_space` | `Detail_blend_noise_space` (`world`, `node`) | `node` | B2.3 |

- BD1.2 The weight attribute is `color_0`; the material stores the channel
  only.
- BD1.3 `Material_texture_samplers` gains the slot `detail_blend`, with
  the texture reference property `detail_blend_texture_property` and the
  slot transform and sampler properties the five existing slots have.
  Every place that enumerates the five slots enumerates six: property
  mirrors, texture reference users, glTF and USD save and load, the
  Properties window, `edit_material` and `set_material_texture_source`,
  the prepared asset and raytrace material paths.
- BD1.4 The values persist the way the other material properties and
  slots persist in each format; a format that has no place for a value
  keeps it in the erhe properties extension of that format.

## BD2 Material record

- BD2.1 The record gains, vec4-aligned and with the struct size a multiple
  of 16 bytes:
  - `detail_blend_texture` (`uvec2` handle) and its `rotation_scale`
    (`vec4`) and `offset` (`vec2`), as the other slots;
  - `detail_blend_parameters` (`vec4`): height influence, transition
    width, noise amount, noise scale;
  - `detail_blend_channels` (`uvec2`): weight channel, height channel, as
    component indices.
- BD2.2 Both record forms (32-bit and 16-bit float) carry the fields. The
  record sizes after the change are stated in `doc/erhe/scene_renderer.md`
  where the current sizes are stated.
- BD2.3 `Material_record_inputs` and `gather_material_record_inputs()`
  carry the new fields; the default records hold the property defaults.

## BD3 Shader variant axes

- BD3.1 `Shader_bool::USE_DETAIL_BLEND`: the material's `detail_blend`.
- BD3.2 `Shader_bool::USE_DETAIL_BLEND_TEXTURE`: `USE_DETAIL_BLEND` and a
  texture in the slot.
- BD3.3 `Shader_bool::USE_DETAIL_BLEND_NOISE`: `USE_DETAIL_BLEND` and a
  noise amount above 0 (B6.1). The amount crossing 0 changes the key, so
  it is part of whatever the draw list hashes for a material's variant
  identity.
- BD3.4 `Shader_int::DETAIL_BLEND_TEXGEN_MODE`: the slot's texgen mode,
  feeding the texcoord and node position varying rules the other slots
  feed.
- BD3.5 `Shader_int::DETAIL_BLEND_NOISE_SPACE`: 0 world, 1 node; node
  turns on `USE_VERTEX_VARYING_NODE_POSITION` when the noise axis is on.
- BD3.6 With `USE_DETAIL_BLEND` off no other axis of this section is set,
  so the material's key and shader are the ones it has today (B6.2).
- BD3.7 The axes are material axes: they stay set under a shader debug
  mode, which reads them for BD5.

## BD4 GLSL

- BD4.1 `res/shaders/erhe_detail_blend.glsl` holds two functions:
  - `erhe_detail_blend(w, h, influence, width, pixel_width)`: B3.2 with
    `s' = max(width, pixel_width, 1e-4)`;
  - `erhe_surface_noise(p)`: 3D value noise, an integer hash at the eight
    lattice corners blended with a smoothstep weight, result in [0, 1].
- BD4.2 `standard.frag`, under `ERHE_USE_DETAIL_BLEND`, computes the
  detail weight once per fragment:
  - `w`: `v_color[weight channel]` with `ERHE_USE_VERTEX_VARYING_COLOR`,
    else 0 (B1.4);
  - height: the slot's texel at the height channel under
    `ERHE_USE_DETAIL_BLEND_TEXTURE`, else 0.5;
  - noise: `erhe_surface_noise(position * noise scale)` under
    `ERHE_USE_DETAIL_BLEND_NOISE`, with the world position or the node
    position by BD3.5; `h = mix(height, noise, noise amount)`;
  - `pixel_width = fwidth(h - 1.0 + w)` (B6.3).
- BD4.3 Under `ERHE_USE_DETAIL_BLEND` the base color and the alpha skip
  their vertex color factor (B1.3). A material whose base color or
  opacity source is `vertex_color` keeps that source.
- BD4.4 In the initial version the detail weight is computed only where
  it is consumed: the debug mode of BD5. A lit variant of a material with
  detail blend on differs from the same material with it off by BD4.3
  only.

## BD5 Debug mode

- BD5.1 `Shader_debug::detail_blend_weight = 38`, display string "Detail
  Blend Weight". `out_color.rgb` is the detail weight; with
  `ERHE_USE_DETAIL_BLEND` off it is 0.
- BD5.2 The mode is reached through the viewport's debug visualization
  combo and the MCP tools `push_shader_debug` / `pop_shader_debug`.

## BD6 CPU reference

- BD6.1 `erhe_math/detail_blend.hpp`: `erhe::math::detail_blend(w, h,
  influence, width, pixel_width) -> float`, `constexpr`, the same
  arithmetic as BD4.1.
- BD6.2 `erhe_math_tests` holds the properties of B3.3 over a grid of
  inputs.

## BD7 Channel painting

- BD7.1 A scene-free core, `tools/channel_brush.{hpp,cpp}`:
  - `Channel_brush_settings`: radius, strength, target, blend mode
    (`mix`, `add`, `subtract`), channel mask (four bits);
  - `Channel_brush_stroke`: per vertex the value before the stroke and
    the greatest brush alpha reached, in buffers that keep their capacity
    between strokes;
  - `apply_dab(settings, stroke, center, positions, values)`: for each
    vertex inside the sphere, alpha = strength times the falloff
    `3p^2 - 2p^3` of its distance; the stroke's alpha for the vertex is
    the maximum over the dabs; the value is the blend of the value before
    the stroke toward the target by that alpha, clamped to [0, 1], in the
    masked channels only. It reports the vertices it changed.
  The core has unit tests in the editor's test executable that holds
  scene-free tool code.
- BD7.2 `Paint_tool` gains the paint mode `Brush` beside `Point`,
  `Corner` and `Polygon`. In `Brush` mode a dab runs `apply_dab()` over
  the hovered primitive's geometry vertices in world space, writes every
  corner of each changed vertex in `corner_color_0` (B7.3), patches the
  GPU buffer as the tool's other modes do, and records before and after
  values in the stroke record the tool already keeps. The stroke ends as
  the one `Paint_colors_operation` per geometry it ends as today. A
  stroke stays on the primitive it started on.
- BD7.3 The channel mask in `Brush` mode: "Weight" masks the one channel
  of B7.4 and paints the target value; "Color" masks all four and paints
  the selected palette color. The tool value that picks the channel when
  the hovered material has detail blend off is `r`, `g`, `b` or `a`.
- BD7.4 Escape during a stroke writes the before values back to the
  geometry and the GPU buffer and queues no operation (B7.5).
- BD7.5 Fill and invert (B7.6) build one compound operation of
  `Paint_colors_operation`, one per selected mesh primitive that has a
  `Geometry`. The channel is the tool's or the call's explicit channel.
- BD7.6 The brush sphere is drawn in `tool_render()` at the hovered
  surface point as three circles of the brush radius; Shift + wheel sets
  the radius through the command pattern of the foliage paint tool
  (B7.7).
- BD7.7 The debug mode switch of B7.8 pushes and pops
  `Shader_debug::detail_blend_weight` on the hovered viewport at the
  click, through the mechanism `push_shader_debug` uses.
- BD7.8 The brush values are tool settings saved with the editor
  settings, as the foliage paint tool's are.

## BD8 MCP

- BD8.1 `edit_material` and `get_material_details` carry the values of
  BD1.1; `set_material_texture_source` accepts the slot `detail_blend`.
- BD8.2 `paint_vertex_channel`: mesh, primitive index, channel, list of
  world-space centers, radius, strength, target, blend mode; one stroke,
  one undo entry, through the code path of BD7.2.
- BD8.3 `fill_vertex_channel` (mesh ids, channel, value) and
  `invert_vertex_channel` (mesh ids, channel): BD7.5.
- BD8.4 Reading weights back uses `get_mesh_attribute_values`.
- BD8.5 The paint tool's brush settings: `get_paint_brush_settings` and
  `set_paint_brush_settings`.

## BD9 Verification

- BD9.1 `scripts/detail_blend_verify.py`, headless, with the groups
  `--only-material` (values and slot set, read back, saved and loaded),
  `--only-paint` (stroke, fill, invert, the other channels untouched,
  undo, the stroke of an MCP call equal to the CPU model in the script),
  `--only-shader` (the image checks of B8.3 through `render_scene_image`
  under the debug mode, and B1.3) and `--only-tool` (the tool's commands
  driven through the input MCP tools: stroke, Escape, Shift + wheel, the
  debug mode switch).
- BD9.2 The height texture of the image checks is a texture the script
  creates through the texture graph tools or a small file it writes.
- BD9.3 The cost measurement of B6.4 is recorded in the implementation
  plan's "Built" when the shader step lands.
