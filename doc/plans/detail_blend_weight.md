# Detail blend weight: requirements

Status: in progress

Requirements for a detailed surface scalar: a low frequency weight painted
per vertex gains high frequency detail from a height texture and a
procedural noise, by height blending (also called height-lerp or
texture-modulated vertex blending). The first use of the scalar is a
material blend weight; the initial version computes the scalar and shows it
as grayscale.

This document states the idea, the terms and the requirements (B-numbers).
Every step is built
(`doc/plans/detail_blend_weight_implementation.md` "Built"). A statement marked "proposed" is the planner's choice
where the idea does not settle the point; "For the user to review" lists
them, and "For the user to rule on" lists the points that need a decision
and whose proposed choice the build follows until then. The design is
`doc/plans/detail_blend_weight_design.md` and the build order
`doc/plans/detail_blend_weight_implementation.md`. The document extends
`doc/erhe/shader_variants.md` (the standard shader and its variant key),
`doc/erhe/primitive.md` (the material) and `doc/editor/tools.md`
(`Paint_tool`).

## The idea

The statements of the idea, each with the requirements that carry it:

1. A surface scalar value, a per vertex material blend weight for example,
   gains detail (B3).
2. Shader cost and resulting quality are balanced (B6).
3. A noise / pattern texture is combined with a noise that depends on the
   surface point and / or normal, and with the interpolated per vertex
   weight (B3, B4).
4. The method is heightmap blending (B3.2).
5. The user provides a per vertex weight, painted to one color channel of
   one vertex color attribute (B1), a height map / detail / blending
   texture (B2.2) and extra parameters (B2.3).
6. The editor paints weights (B7).
7. The editor assigns the height map and the extra parameters (B2.5).
8. The initial version renders high frequency grayscale from the painted
   vertex weights, the texture and the other parameters (B5).

## Where the code stands

- A geometry holds two color sets, `color_0` and `color_1`, each on facets,
  vertices and corners (`erhe_geometry/geometry.hpp`). The primitive builder
  writes both when the vertex format asks for them; the vertex formats of
  `Mesh_memory` ask for `color` 0 only, as a float `vec4`, and the
  optimized variant stores it as four unorm8.
- The standard shader multiplies the material's base color by the `rgb` of
  vertex color 0 and the fragment alpha by its `a`, or takes them from the
  vertex color alone (`Material_input_source`, `standard.frag`). A mesh
  without the attribute reads a constant `vec4(1.0)`.
- A material has five texture slots (`Material_texture_samplers`: base
  color, metallic-roughness, normal, occlusion, emissive). Each slot is a
  registered property with a texture reference (a `Texture`, a
  `Graph_texture`, a rendergraph node), sampler state, a `Texgen_mode`
  (three UV sets, world and node space planes, tangent) and a rotation,
  offset and scale. A scalar input names the channel it reads with a
  `Texture_channel` property.
- The material record is 368 bytes, or 224 bytes with 16-bit floats on the
  headset, the detail blend fields included (`doc/erhe/scene_renderer.md`).
- `Shader_debug` is the per-viewport debug visualization: each value
  compiles a variant of the standard shader that overrides the final
  fragment color ("Vertex Color RGB", "Vertex Color Alpha", "Joint Weight
  Ramp" and the others).
- `Paint_tool` paints one palette color, all four channels, onto the
  point, corner or polygon under the pointer. It writes the geometry's
  `corner_color_0` and the GPU buffer during the stroke, and the stroke is
  one undoable `Paint_colors_operation`. It has no brush radius, falloff or
  strength. A primitive without a `Geometry` gets the GPU preview only.
- `Weight_paint_tool` paints joint weights with a world-space sphere brush:
  smooth falloff, mix / add / subtract toward a target value, overlapping
  dabs of one stroke converging to the target (`doc/editor/weight_paint.md`).
- The texture graph produces noise and pattern textures inside the editor
  (`doc/editor/texture_graph.md`), usable in any material slot.

## Terms

- Weight: the low frequency scalar in [0, 1], one value per vertex,
  interpolated across the triangle.
- Weight source: the vertex color attribute and the channel that hold the
  weight (B1).
- Height: the high frequency scalar in [0, 1] read from the height texture
  (B2.2).
- Surface noise: a procedural scalar in [0, 1] computed in the shader from
  the surface point (B4).
- Transition width: the share of the weight range over which one fragment
  goes from 0 to 1 (B3.2).
- Detail weight: the result, a scalar in [0, 1] per fragment (B3).
- Detail blend: the material feature that computes the detail weight; a
  material has it on or off (B2.1).

## Requirements

### B1 Weight source

- B1.1 The weight source is a material value: a vertex color attribute and
  one of its channels `r`, `g`, `b`, `a`. Proposed: the attribute is
  `color_0`, the only color set the vertex formats carry.
- B1.2 The weight is one value per vertex: every corner of a vertex holds
  the same value in the weight channel, so the weight is continuous across
  edges.
- B1.3 A color attribute named as a weight source is data: a material with
  detail blend on multiplies neither its base color nor its alpha by that
  attribute (proposed; "For the user to rule on" 1).
- B1.4 A mesh without the attribute has weight 0 everywhere (proposed).
- B1.5 The weight keeps its precision of the vertex format: 8 bits in the
  optimized variant. The detail weight shows no banding from it at the
  default parameters, because the height dithers the steps.

### B2 Material values

- B2.1 A material has a detail blend switch, off by default. A material
  with the switch off compiles to the shader it compiles to today, and
  the record fields it has today hold the values they hold today.
- B2.2 The height texture is a sixth material texture slot with everything
  a slot has: texture reference, sampler state, texgen mode, rotation,
  offset, scale. A `Texture_channel` value names the channel read as the
  height. With no texture in the slot the height is 0.5 everywhere.
- B2.3 The extra parameters, each a registered material property:
  - height influence, 0 - 1, default 1: 0 makes the detail weight equal to
    the weight;
  - transition width, 0 - 1, default 0.2;
  - surface noise amount, 0 - 1, default 0: the share of the surface noise
    in the combined height (B4.3);
  - surface noise scale, in cycles per meter, default 1;
  - surface noise space, world or node, default node.
- B2.4 Every value of B1.1, B2.1 - B2.3 is saved and loaded with the
  material, copied with it, inherited through styles and undoable, as the
  existing material properties are.
- B2.5 The Properties window shows the values of a selected material in a
  "Detail Blend" group; the texture slot row is the row the other slots
  have. An edit takes effect on the frame it is made.

### B3 Detail weight

- B3.1 The inputs are the weight `w` (B1), the combined height `h` (B4.3),
  the height influence `i` and the transition width `s`.
- B3.2 The detail weight is the height blend (proposed form; the design
  may choose another with the properties of B3.3):
  `d = saturate((h - 1 + w * (1 + s')) / s')`, with `s'` the transition
  width after the filter of B6.3, and the result
  `mix(w, d, i)`.
- B3.3 The properties that hold for every parameter value:
  - `w = 0` gives 0 and `w = 1` gives 1, whatever the height;
  - the result rises with `w` and with `h`, never falls;
  - at a weight between 0 and 1 the fragments with the greatest height
    reach 1 first: the painted boundary follows the height texture's
    pattern;
  - a transition width near 0 gives a hard edge at `h = 1 - w`, a width of
    1 a soft ramp.
- B3.4 The function exists once in GLSL and once as a CPU reference in
  C++, and a unit test holds the CPU reference to B3.3.

### B4 Surface noise

- B4.1 The surface noise is computed in the fragment shader from the
  surface point in the space of B2.3, scaled by the surface noise scale.
  Its purpose is to break the repetition of a tiling height texture and to
  give detail to a material with no height texture.
- B4.2 The noise is one evaluation of a value or gradient noise, without a
  texture read. It is continuous across the surface and fixed to the
  surface: a node-space noise moves with the node, a world-space noise
  stays in the world.
- B4.3 The combined height is
  `mix(height, surface noise, surface noise amount)`.
- B4.4 A term that depends on the surface normal (a slope bias: more
  weight on faces that look up) is future work (proposed; "For the user to
  rule on" 3).

### B5 Initial version: grayscale

- B5.1 `Shader_debug` gains the value "Detail Blend Weight": the fragment
  color is the detail weight as linear grayscale, 0 black and 1 white.
- B5.2 A mesh whose material has detail blend off shows black in this
  mode.
- B5.3 The mode shows the full detail: height texture pattern, surface
  noise and painted weight together, updated live while a stroke of B7 is
  painted and while a value of B2 is edited.
- B5.4 Outside the debug mode a material with detail blend on shades as it
  does with the switch off, except for B1.3. Consumers of the detail
  weight are future work.

### B6 Cost and quality

- B6.1 A fragment of a material with detail blend on costs, beyond today's
  shader: one texture sample (the height) and one noise evaluation. A
  surface noise amount of 0 costs no noise evaluation.
- B6.2 Detail blend is a shader variant axis derived from the material
  (B2.1), so every other material's shader is unchanged.
- B6.3 The transition is antialiased: the effective transition width is at
  least the change of the combined height across one pixel, so a hard edge
  stays a one pixel ramp at any distance and under minification. The
  height texture is sampled with its mipmaps.
- B6.4 The cost is measured before the feature is called done: the
  standard shader's instruction count with and without the axis from the
  shader analysis (`doc/editor/shader_analytics.md`), on the desktop
  variant and the headset variant, recorded in the design document.
- B6.5 The 16-bit material record holds the new values. The design states
  the record size after the change.

### B7 Weight painting

- B7.1 The editor paints the weight channel of the weight source with a
  brush: a world-space sphere centred on the surface point under the
  pointer, with a radius, a strength, a target value in [0, 1], a smooth
  falloff and the modes mix, add and subtract. Overlapping dabs of one
  stroke converge to the target value.
- B7.2 A stroke writes the one weight channel and leaves the other three
  channels of every touched corner as they are.
- B7.3 A stroke writes all corners of a touched vertex alike (B1.2).
- B7.4 The channel painted is the weight source of the material on the
  hovered primitive; a tool value overrides it for painting a mesh whose
  material has detail blend off.
- B7.5 The stroke is visible while it is painted, in the viewport's
  current display, and is one undo entry. Escape during a stroke restores
  the values the stroke changed.
- B7.6 Two commands beside the brush: fill (the target value on every
  vertex of the selected meshes) and invert (`1 - weight` on the same),
  each one undo entry.
- B7.7 The brush shows its sphere and its radius in the viewport while the
  tool is active. Shift and the wheel set the radius, as in the foliage
  paint tool.
- B7.8 The tool's properties offer a switch that sets the hovered
  viewport's shader debug mode to "Detail Blend Weight" and back.
- B7.9 Painting needs a primitive with a `Geometry`. On a primitive
  without one the tool paints nothing and states the reason in its
  properties.
- B7.10 Proposed: the brush is a mode of `Paint_tool` (which gains radius,
  falloff, strength and a channel mask), so color painting gets the same
  brush.

### B8 MCP and verification

- B8.1 MCP tools with explicit arguments: set and read the detail blend
  values of a material, paint a stroke (mesh, primitive, list of
  world-space brush centers, radius, strength, target, mode), fill, invert,
  and read back the weights of a primitive.
- B8.2 `Shader_debug` "Detail Blend Weight" is selectable through the
  existing MCP tool that sets the viewport's debug mode.
- B8.3 A headless verify script checks: a filled weight of 0 renders
  black and of 1 renders white with any height texture; a weight gradient
  across a plane with a checker height texture renders the boundary at
  `h = 1 - w` within one pixel at transition width 0; a stroke changes
  only the weight channel; undo restores the image; save and load keeps
  the values and the image.
- B8.4 The unit test of B3.4.

## Future work

- Consumers of the detail weight: blending two materials or two layers of
  one material (base color, roughness, normal), with the height texture
  taken from the layer's own height where it has one.
- More than one weight per mesh (one per channel) for more than two
  layers.
- The slope bias of B4.4 and other normal dependent terms.
- Triplanar sampling of the height texture.
- A dedicated weight attribute outside the color sets, with its glTF and
  USD names.
- Brushes beyond B7: blur, smear, symmetry (shared with
  `doc/plans/weight_paint.md`).

## For the user to rule on

1. Where the weight lives. `color_0` is on the GPU today but is also the
   mesh's color: B1.3 makes it data for a material with detail blend on,
   and a file exported with weights in `COLOR_0` shows them as a tint in
   other viewers. The alternative is `color_1` in the vertex formats: 4
   more bytes per vertex in the optimized variant and 16 in the float one,
   for every mesh unless the format becomes per mesh, and a glTF
   `COLOR_1`. Proposed: `color_0` for the initial version.
2. What the initial version shows. Proposed: the `Shader_debug` mode of
   B5. The alternative is a material that outputs the grayscale as its
   base color, visible without a debug mode.
3. The normal dependent term. The idea says "surface point and / or
   normal"; B4 takes the point only. A slope bias needs one more parameter
   pair (direction, strength) and no texture read.
4. The painting tool. Proposed: `Paint_tool` gains the brush (B7.10). The
   alternative is a tool of its own, leaving `Paint_tool` as it is.
5. Whether the headset build compiles the surface noise, or the headset
   variant is height texture only.

## For the user to review

- B1.4: weight 0 on a mesh without the attribute, where the shader's
  constant for a missing color is 1.
- B2.2: no texture reads as height 0.5.
- B2.3: the parameter list and its defaults; node space as the default
  noise space.
- B3.2: the form of the blend function.
- B6.3: the pixel-wide lower bound of the transition width.
- B7.6: fill and invert act on the selected meshes, whole.
- B7.9: no painting on a primitive without a `Geometry`.
