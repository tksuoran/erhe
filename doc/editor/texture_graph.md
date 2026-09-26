# Procedural texture graph

Stability: mostly stable

The editor's procedural texture node graph: a port of Material Maker's GLSL
composition model onto `erhe::graph`, `erhe::texgen` and the editor's graph
window. This document holds the architecture of the port, what the node library
covers against Material Maker's, and how it is verified.

Reference implementation: https://github.com/RodZill4/material-maker
(clone it locally to read the node definitions; MIT license - GLSL snippets
ported from it carry an attribution comment). Referred to below as
`<material-maker>`; record your own clone location in `memory-bank/local/`.

## Table of Contents

1. [Node Library Comparison](#node-library-comparison-erhe-vs-material-maker)
2. [Material Maker Architecture](#material-maker-architecture)
3. [erhe Existing Infrastructure](#erhe-existing-infrastructure)
4. [Architecture Decisions](#architecture-decisions)
5. [Verification Strategy](#verification-strategy)
6. [Key Files Reference](#key-files-reference)
7. [Future work](#future-work)

---

## Node Library Comparison (erhe vs Material Maker)

Compared against Material Maker master: 392 `.mmg` node definitions under
`addons/material_maker/nodes/` plus ~19 engine-level node types in
`engine/nodes/gen_*.gd` (buffer, switch, image, text, graph/group, remote,
comment, export, iterate_buffer, ...). erhe has 82 node types: 79 descriptors
in `src/editor/texture_graph/nodes/texture_node_descriptors.cpp` plus the
descriptor-less `output`, `material_output` and `buffer` nodes
(`texture_graph_node_factory.cpp`). The 392 count includes internal companion
sub-nodes of compound graphs (`edge_detect_1..3`, `fill_preprocess`, ...), so
the user-visible Material Maker library is somewhat smaller than the raw file
count.

**Vertical orientation.** The texgen fullscreen triangle's vertex shader takes
the uv sign from `device.get_info().coordinate_conventions.texture_origin`, as
all application code must (see the `Y_SIGN` note in `erhe::graphics`
`test_topology`), so row 0 is `uv.y = 0` on every backend and an erhe-composed
image agrees with Material Maker, whose GLSL these nodes are ported from.
Deriving `v_uv` straight from the NDC position instead hardcodes the
`bottom_left` (OpenGL) texture origin, which stores composed textures
vertically mirrored against their own shader uv space on Vulkan and Metal: node
previews and PNG exports come out upside down, a `seven_segment` 2 reads as a
5, and `profile`'s Fill style fills the top. `section_patterns` asserts the
mapping, and that a Buffer node round-trips it unchanged.

### erhe nodes and their Material Maker counterparts

| erhe node             | Category   | Material Maker source        | Coverage notes |
|-----------------------|------------|------------------------------|----------------|
| `uniform`             | Generators | `uniform.mmg`                | Full. |
| `uniform_greyscale`   | Generators | `uniform_greyscale.mmg`      | Full. |
| `perlin`              | Generators | `perlin.mmg`                 | Full grayscale port. MM also has `perlin_color`. |
| `voronoi`             | Generators | `voronoi.mmg`                | 3 of 4 outputs (nodes, borders, random color); the companion-node "Fill" output is dropped. |
| `bricks`              | Generators | `bricks.mmg`                 | Grayscale pattern output only; per-brick random-color / UV / corner outputs and mortar/bevel/round map inputs dropped. 5 bond patterns. |
| `shape`               | Generators | `shape.mmg`                  | 3 of 5 shapes (circle, polygon, star); curved-star and rays shapes plus size/edge map inputs dropped. |
| `fbm`                 | Generators | `fbm.mmg`                    | 4 bases (value, perlin, cellular, cellular2); octave loop inlined instead of per-instance helper function. MM's `fbm2`/`fbm3`/`fbm4` are the same node with larger basis libraries; see `doc/plans/texture_graph.md`. |
| `noise`               | Generators | `noise.mmg`                  | Density map input dropped (parameter only). |
| `color_noise`         | Generators | `color_noise.mmg`            | Full. |
| `perlin_color`        | Generators | `perlin_color.mmg`           | Full. |
| `voronoi_triangle`    | Generators | `voronoi_triangle.mmg`       | Full (all 5 outputs). Helpers prefixed `tri_*` to keep generic names out of the shared global namespace. |
| `wavelet_noise`       | Generators | `wavelet_noise.mmg`          | Full (5 combine modes). |
| `noise_anisotropic`   | Generators | `noise_anisotropic.mmg`      | Full. |
| `noise_white`         | Generators | `noise_white.mmg`            | Full; MM's instance wrapper written directly (one call site). |
| `shard_fbm`           | Generators | `shard_fbm.mmg`              | Full; `hash` renamed `shard_hash`, and `pow(2, ...)` corrected to `pow(2.0, ...)` for glslang. |
| `gradient`            | Gradients  | `gradient.mmg`               | Full (repeat / rotate / mirror + gradient widget). |
| `circular_gradient`   | Gradients  | `circular_gradient.mmg`      | Full. |
| `radial_gradient`     | Gradients  | `radial_gradient.mmg`        | Full. |
| `spiral_gradient`     | Gradients  | `spiral_gradient.mmg`        | Full; MM's bare `circular_gradient` helper renamed `mm_spiral_angle`. |
| `multigradient`       | Gradients  | `multigradient.mmg`          | Full (rgb input warps uv and offsets the seed). |
| `sine_wave`           | Patterns   | `sine_wave.mmg`              | Full. |
| `truchet`             | Patterns   | `truchet.mmg`                | Line and circle tiles. MM also has `truchet_generic`. |
| `weave`               | Patterns   | `weave.mmg`                  | Width map input dropped. MM also has `weave2`, `diagonal_weave`, `weave_random`. |
| `pattern`             | Patterns   | `pattern.mmg`                | Full (6 waveforms x 6 combiners). `wave_*` / `mix_*` prefixed `pat_`. |
| `beehive`             | Patterns   | `beehive.mmg`                | Full, including the random-color and per-cell UV map outputs. |
| `cairo`               | Patterns   | `cairo.mmg`                  | Pattern output only; the `fill` output needs the Fill family's iterate-buffer machinery, so `cairo_bbox` is not carried over. |
| `arc_pavement`        | Patterns   | `arc_pavement.mmg`           | Full, including the random-color and brick UV outputs. MM's `PI` spelled out; `pavement` prefixed `ap_`. |
| `iching`              | Patterns   | `iching.mmg`                 | Full. Grid-size default taken from the .mmg instance (4); the shader_model default of 0 is below its own minimum of 2. |
| `runes`               | Patterns   | `runes.mmg`                  | Full; sdline2's `sdLine` inlined as `rune_sd_line`. Grid-size default as for `iching`. |
| `roman_numerals`      | Patterns   | `roman_numerals.mmg`         | Full; `sdLine` inlined as `rn_sd_line`. Two glslang fixes (int literals in `clamp`, brace-initialized `const int val[]`) and defaults taken from the .mmg instance - the shader_model `bevel` of 0.5 leaves a peak output of 0.03. |
| `seven_segment`       | Patterns   | `seven_segment.mmg`          | Full (multi-digit). MM's `m` / `fs` / `ssd` / `ssd_multi` prefixed `ss_`. |
| `scratches`           | Patterns   | `scratches.mmg`              | Full. |
| `profile`             | Patterns   | `profile.mmg`                | Full (both styles), using the existing gradient parameter widget. |
| `japanese_glyphs`     | Patterns   | `japanese_glyphs.mmg`        | Full (46 hiragana + 46 katakana, single glyph or grid). The largest port in the library; transcribed from the .mmg by a generator. `curve` / `sdline2` / `sdbox` includes inlined, every symbol prefixed `jg_` (MM's bare `s`, `l`, `box`, `grid`). |
| `blend`               | Filters    | `blend.mmg`                  | 14 modes; mask input dropped (opacity parameter only); "Linear Light" mode dropped (MM ships no implementation). |
| `colorize`            | Filters    | `colorize.mmg`               | Full, including gradient widget codegen. |
| `curve`               | Filters    | `curve.mmg` (widget)         | Hermite tone curve per RGB channel, alpha pass-through. |
| `transform`           | Filters    | `transform.mmg`              | Per-channel map inputs (tx/ty/r/sx/sy) dropped; scalar parameters only. |
| `rotate`              | Transform  | `rotate.mmg`                 | Full. |
| `scale`               | Transform  | `scale.mmg`                  | Full. |
| `shear`               | Transform  | `shear.mmg`                  | Full. |
| `skew`                | Transform  | `skew.mmg`                   | Full. |
| `mirror`              | Transform  | `mirror.mmg`                 | Full. |
| `repeat`              | Transform  | `repeat.mmg`                 | Full. |
| `swirl`               | Transform  | `swirl.mmg`                  | Full; the rotation is written into the helper instead of calling a shared `rotate()` (globals dedup by exact string, so a shared symbol inside two globals would double-define). |
| `spherize`            | Transform  | `spherize.mmg`               | Full, including the mask and field outputs. |
| `magnify`             | Transform  | `magnify.mmg`                | Full. |
| `kaleidoscope`        | Transform  | `kaleidoscope.mmg`           | "Variations" dropped (needs MM's input-variation machinery); the helper still returns the sector index. |
| `warp`                | Transform  | `warp.mmg`                   | Full (both modes). MM's `$(name)_slope` instance helper is written inline - erhe descriptors have no instance stanza. |
| `directional_warp`    | Transform  | `directional_warp.mmg`       | Full. |
| `refract`             | Transform  | `refract.mmg`                | Full; instance helper inlined as for `warp`. |
| `brightness_contrast` | Filters    | `brightness_contrast.mmg`    | Full. |
| `normal_map`          | Filters    | `normal_map.mmg`             | MM's compound graph (buffer + switch + edge detect) flattened to the edge-detect core; "Default" output format only. |
| `blur`                | Filters    | `gaussian_blur_x/_y.mmg`     | Self-contained 2D 13x13 kernel instead of MM's separable X/Y pair around buffers; composes without a buffer (expensively). |
| `math`                | Filters    | `math.mmg`                   | 18 operations. MM also has `math_v3` for vec3. |
| `invert`              | Filters    | `invert.mmg`                 | Full. |
| `quantize`            | Filters    | `quantize.mmg`               | Full. |
| `adjust_hsv`          | Filters    | `adjust_hsv.mmg`             | Full. MM also has `alter_hsv`. |
| `remap`               | Filters    | `remap.mmg`                  | Full. |
| `combine`             | Channels   | `combine.mmg`                | Full. |
| `decompose`           | Channels   | `decompose.mmg`              | Full (4 grayscale outputs). |
| `swap_channels`       | Channels   | `swap_channels.mmg`          | Full (10 sources per output channel). |
| `greyscale`           | Color      | `greyscale.mmg`              | Full (5 weightings). Input typed rgba, not MM's rgb: pins are type-strict here and rgba is what the color producers emit. |
| `tones`               | Color      | `tones.mmg`                  | Full levels control (in black/mid/white, out black/white). |
| `tones_map`           | Color      | `tones_map.mmg`              | Full. |
| `tones_range`         | Color      | `tones_range.mmg`            | Full. |
| `tones_step`          | Color      | `tones_step.mmg`             | Full. |
| `tonality`            | Color      | `tonality.mmg`               | Full (grayscale tone curve; `curve` is the per-RGB-channel sibling). |
| `convert_colorspace`  | Color      | `convert_colorspace.mmg`     | Full (RGB / HSV / YUV, both directions). |
| `colormap`            | Color      | `colormap.mmg`               | Full. |
| `palettize`           | Color      | `palettize.mmg`              | Full; MM's instance helper inlined as for `warp`. |
| `default_color`       | Color      | `default_color.mmg`          | Full. |
| `compare`             | Color      | `compare.mmg`                | Full. |
| `ensure_greyscale`    | Channels   | `ensure_greyscale.mmg`       | Re-purposed: MM's is a no-op coercion, this is the explicit rgb -> f bridge type-strict pins need. |
| `ensure_rgba`         | Channels   | `ensure_rgba.mmg`            | Re-purposed likewise as the f -> rgba bridge. |
| `reroute`             | Utility    | `gen_reroute.gd` (engine)    | Pass-through wiring helper, same concept. |
| `switch` / `switch_grayscale` / `switch_rgb` | Utility | `gen_switch.gd` (engine) | Compile-time branch select, same semantics (the unselected branch emits no code). MM's node is dynamic-arity and typed "any"; erhe pins are static and per-type, so this is a fixed 4-choice switch registered once per value type. |
| `buffer`              | Utility    | `gen_buffer.gd` (engine)     | Explicit RTT cut point, same semantics (size + format, dirty-driven re-render). |
| `output`              | Output     | (none)                       | erhe-specific: bakes one texture into `Content_library`, optional assign-to-material. MM's nearest concept is the material node. |
| `material_output`     | Output     | `material.mmg`               | PBR channel bake. MM also has `material_3d`, `material_unlit`, `material_dynamic`, `material_tesselated`, `material_raymarching` variants. |

### Material Maker families beyond erhe's original node set

Counts are approximate `.mmg` file counts per family. Each family is scored so
the ones still missing can be ordered, and the table is sorted by score,
highest first:

- **Cost** (1-5): implementation effort. 1 = trivial descriptor ports using
  existing `erhe::texgen` features (widgets, function-form inputs, buffers);
  3 = a new value type or a family of many small nodes; 5 = new engine
  machinery (iterate buffers, paint pipeline) or a scope change.
- **Benefit** (1-5): material-authoring value added in erhe, given what the
  existing nodes already cover.
- **Score** = Benefit / Cost. Coarse estimates for ordering the backlog, not
  commitments.

Cost and benefit scores are the estimates made when the family was first
ranked and are deliberately not restated afterwards, so a family erhe has
since ported shows what it was predicted to take. Ported families keep their
row rather than being deleted, so the table stays a complete comparison; the
ones still missing are the backlog, prioritized in
`doc/plans/texture_graph.md`.

| Material Maker family        | ~Count | Representative nodes | Cost | Benefit | Score | erhe status |
|------------------------------|--------|----------------------|------|---------|-------|-------------|
| Gradients                    | 5      | `gradient`, `circular_gradient`, `radial_gradient`, `spiral_gradient`, `multigradient` | 1 | 4 | 4.0 | All 5 ported as descriptors in a new "Gradients" palette category. |
| Switch                       | 1      | `switch` (engine, `gen_switch.gd`) | 1 | 3 | 3.0 | Registered once per value type (`switch`, `switch_grayscale`, `switch_rgb`) because erhe pins are type-strict; 4 choices each. |
| Image / texture input        | 2      | `image`, `texture` (engine) | 2 | 5 | 2.5 | Missing. Sample an external bitmap as a graph source; the `buffer` node already proves `sampler2D`-backed expressions downstream. |
| Transform / UV warps         | 25     | `translate`, `rotate`, `scale`, `shear`, `skew`, `warp`, `directional_warp`, `multi_warp`, `warp_dilation*`, `swirl`, `twist`, `spherize`, `kaleidoscope`, `mirror`, `repeat`, `custom_uv`, `distort`, `refract`, `magnify` | 2 | 5 | 2.5 | 13 of ~25 ported: rotate, scale, shear, skew, mirror, repeat, swirl, spherize, magnify, kaleidoscope, warp, directional_warp, refract. Still missing: `multi_warp` (compound node), `distort` (needs a lattice widget type), `custom_uv` (tileset + variation machinery), `twist` (sdf3d, out of scope), `warp_dilation*` (multi-pass buffers). |
| Color / tone filters         | 18     | `auto_tones`, `tonality`, `tones`, `tones_map/range/step`, `palettize`, `colormap`, `convert_colorspace`, `greyscale`, `ensure_greyscale`, `ensure_rgba`, `default_color`, `compare` | 2 | 4 | 2.0 | 14 nodes ported. Still missing: `auto_tones` (no shader_model - needs a min/max reduction over the whole image, which the composer cannot express). `tones` needed no levels widget after all: it is five color parameters. |
| Noise variants               | 15     | `voronoi2`, `voronoi_triangle`, `clouds_noise`, `wavelet_noise`, `noise_anisotropic`, `noise_white`, `directional_noise`, `perlin_color`, `crystal`, `shard_fbm`, `fbm2..4` | 2 | 3 | 1.5 | 6 nodes ported. Deliberately not ported: `voronoi2` (byte-identical to `voronoi.mmg`, which the existing `voronoi` node already is - only its `fill` output differs, and that needs the Fill family's iterate-buffer machinery), `clouds_noise` / `directional_noise` / `crystal` (compound nodes, no shader_model), and `fbm2..4` (13-17 KB of near-duplicate basis libraries; their extra bases belong on the existing `fbm` node's enum instead). |
| Deterministic patterns       | 20     | `pattern`, `arc_pavement`, `beehive`, `cairo`, `iching`, `runes`, `japanese_glyphs`, `roman_numerals`, `seven_segment`, `scratches`, `splines`, `polycurve`, `profile`, `dirt` | 2 | 3 | 1.5 | 11 nodes ported. Deliberately not ported: `splines` and `polycurve` (both driven by a Material Maker point-list parameter widget - "splines" / "polyline" - whose GLSL is generated per instance from the edited points; erhe has no such `Parameter_kind`, and adding one is a widget + parameter-codegen feature rather than a GLSL port), and `dirt` (a compound graph node, 26 sub-nodes, no `shader_model`). `cairo`'s `fill` output was dropped for the same reason as the Fill family generally. |
| 2D SDF                       | 51     | `sdcircle`, `sdbox`, `sdline`, `sdpolygon`, `sdstar`, `sdboolean`, `sdsmoothboolean`, `sdrepeat`, `sdmorph`, `sdshow` | 3 | 4 | 1.3 | Missing; needs an `sdf2d` value type plus shape / ops / stroke / fill nodes. New value type up front, then many tiny nodes; crisp resolution-independent shape authoring. |
| Height / normal / AO         | 12     | `normal2height`, `normal_blend`, `normal_map2`, `normal_map_convert`, `height_to_angle`, `height_to_offset`, `occlusion`, `hbao`, `slope`, `smooth_curvature` | 3 | 4 | 1.3 | Only `normal_map` covered. Key for PBR authoring; several need buffers (exist). |
| Brick / weave variants       | 13     | `bricks2`, `bricks3`, `bricks_uneven*`, `skewed_bricks`, `weave2`, `diagonal_weave`, `weave_random` | 2 | 2 | 1.0 | Base `bricks`/`weave` covered; variants are straight ports with diminishing returns. |
| Tiling / splatter            | 10     | `tiler`, `tiler_advanced`, `splatter`, `circle_splatter`, `tile2x2`, `make_tileable` | 4 | 4 | 1.0 | Missing. Signature MM feature for organic materials, but `tiler`/`splatter` are complex compound nodes. |
| Bit packing                  | 4      | `pack_1x32_to_2x16`, `pack_2x16_to_1x32`, ... | 1 | 1 | 1.0 | Missing. Trivial ports, niche use. |
| Image-processing filters     | 30     | `sharpen`, `emboss`, `edge_detect`, `dilate`, `morphology`, `denoiser`, `*_kuwahara`, `symmetric_nearest_neighbor`, `pixelize`, `supersample`, `fast_blur`, `directional_blur`, `slope_blur`, `bevel` | 4 | 3 | 0.75 | Only `blur` covered. Mostly buffer-dependent multi-pass filters. |
| Node groups (subgraphs)      | 1      | `graph` (engine, `gen_graph.gd`) | 4 | 3 | 0.75 | Missing; the geometry graph's Group pattern is the precedent. Payoff grows with library size. |
| Fill family                  | 25     | `fill`, `fill_to_color`, `fill_to_gradient`, `fill_to_position`, `fill_to_random_color`, `fill_to_size`, `fill_to_uv`, `rgba_to_fill` | 5 | 3 | 0.6 | Missing; requires iterate-buffer machinery (`gen_iterate_buffer.gd`), which erhe does not have. |
| Variations / randomization   | 11     | `variations_*`, `randomize`, `controlled_variations`, `iterate_variations`, `layer_variations` | 4 | 2 | 0.5 | Missing; the per-node seed and reseed system covers part of the use case. |
| Editor conveniences          | ~8     | `comment`, `remote`, `export`, `portal`, `text`, `webcam` (engine) | 2 | 1 | 0.5 | Missing; low value in erhe (PNG export and MCP scripting already cover the main uses). |
| 3D SDF + raymarching         | 42     | `sdf3d_sphere`, `sdf3d_box`, `sdf3d_boolean`, `sdf3d_revolution`, `raymarching`, `material_raymarching` | 5 | 2 | 0.4 | Out of scope (no `sdf3d` type planned). |
| 3D / mesh-based textures     | 42     | `tex3d_*` (36), `mesh`, `mesh_curvature`, `brush_triplanar`, `sphere`, `box`, `mwf_*` workflow nodes | 5 | 1 | 0.2 | Out of scope (erhe texture graph is 2D `vec2 uv` only). |
| Painting                     | 3      | `brush`, `brush_3d`, `brush_select_from_id` | 5 | 1 | 0.2 | Out of scope (interactive paint pipeline). |

---

## Material Maker Architecture

Material Maker (Godot 4, GDScript) is the reference. The load-bearing facts
for the port, established by reading `<material-maker>`:

### GLSL source composition, not per-node render-to-texture

Each node contributes GLSL *snippets*; the engine composes one monolithic
shader per rendered output and runs it once into a texture. There is no
per-node RTT except at explicit "buffer" nodes. The central data structure is
`ShaderCode` (`addons/material_maker/engine/nodes/gen_base.gd`):

- `globals[]` - function definitions, de-duplicated by exact code match
- `uniforms[]` - scalar/color parameters (live-editable without recompile)
- `defs` - per-node uniform declarations + emitted input-helper functions
- `code` - inline statements (shared subexpressions)
- `output_values[type]` - GLSL *expression* per output type, with `$uv` still
  unresolved - a node output is "an expression that, given uv, yields a value"

Composition (`gen_shader.gd::_get_shader_code`) walks the graph from an output
back through inputs. When node A samples input `s1` at a coordinate
(`$s1($uv + vec2(0.01, 0.0))`) the engine either *inlines* the source
expression with `$uv` rebound, or emits a helper function
`oNNN_input_s1(vec2 uv, ...)` wrapping the source subtree and substitutes a
call - the function form is used for multiply-sampled inputs (blur, warp) and
controlled by the input's `function: true` flag plus a variant-tracking
context (`gen_context.gd`) that de-duplicates repeated subtrees.

Final assembly fills a shader template (`buffer_compute.tres`):
`@COMMON_SHADER_FUNCTIONS` (rand/hash library) + `@GLOBALS` + `@DEFINITIONS`
+ `void main() { vec2 uv = ...; @CODE; result = @OUTPUT_VALUE; }`.

### Node definition format (.mmg)

Nodes are JSON data (`addons/material_maker/nodes/*.mmg`), ~400 of them. The
`shader_model` schema:

- `inputs[]`: `{name, type, default (GLSL expr used when unconnected),
  [function: true]}`
- `outputs[]`: `{type, <typename>: "<GLSL expression>"}` e.g.
  `"f": "perlin($(uv), vec2($(scale_x), $(scale_y)), ...)"`
- `parameters[]`: `{name, type (float|color|enum|boolean|size|gradient|curve|
  polygon|...), default, min, max, step, values[]}`
- `global`: GLSL function library injected once
- `code`: inline statements run before the output expression
- `includes`: named shared function libraries

Substitution grammar (`gen_shader.gd::replace_variables`):

- `$name` / `$(name)` - parameter value or built-in; `float` becomes a
  uniform `p_oNNN_name`, `enum` substitutes its `value` string *as a code
  fragment* (`blend_$blend_type` -> `blend_multiply`), `boolean` a literal,
  `size` -> `pow(2, v)`, `gradient`/`curve` -> a generated GLSL function
- built-ins: `$uv` (current coordinate expr), `$seed`, `$time`,
  `$name` -> unique node id `oNNN`, `$name_uv` -> variant-unique id
- `$input_name(coord)` - sample an input at a coordinate (inline or call)
- `$rnd(a, b)` -> `param_rnd(a, b, $seed + <positional offset>)`

### Type system (io_types.mmt)

| name    | GLSL  | signature | slot | meaning                |
|---------|-------|-----------|------|------------------------|
| `f`     | float | `vec2 uv` | 0    | grayscale              |
| `rgb`   | vec3  | `vec2 uv` | 0    | color                  |
| `rgba`  | vec4  | `vec2 uv` | 0    | color + alpha          |
| `sdf2d` | float | `vec2 uv` | 1    | 2D signed distance     |
| `sdf3d` | float | `vec3 p`  | 2    | 3D signed distance     |
| ...     |       |           |      |                        |

Connection compatibility = matching `slot` integer (`f`/`rgb`/`rgba` freely
interconnect); conversions are expression-rewrite templates
(`f`->`rgb` = `vec3($(value))`, `rgba`->`f` = `dot(rgb, vec3(1))/3`).

### Buffers, seeds, widgets, material output

- **Buffer node** (`gen_buffer.gd`): render my input subtree once into a real
  texture at a fixed resolution, expose it as a `sampler2D` - the explicit
  RTT cut point that stops expensive subtrees being inlined at every sample
  site. Driven by a dependency/dirty system.
- **Seeds**: `$seed` is a per-node uniform; seeds cascade from parent graphs;
  hash functions (`rand`/`rand2`/`rand3`) live in the common library.
- **Gradient/curve widgets** compile to GLSL helper functions plus uniform
  arrays of control points; only structural edits (add/remove stop) recompile.
- **Material node** (`material.mmg`): a node whose *inputs are the PBR
  channels* (albedo rgb, metallic f, roughness f, emission rgb, normal rgb,
  occlusion f, depth f, opacity f); each connected channel is baked to its own
  texture at a power-of-two size (16^2..8192^2, default 1024^2). Export packs
  channels (ORM) and emits engine-specific descriptors from text templates.

### MVP node subset

`uniform` (color constant), `perlin`, `voronoi`, `bricks`, `shape`, `blend`,
`colorize`, `transform`, `brightness_contrast`, `normal_map`, plus an output
node - covers the canonical noise -> warp -> colorize -> blend -> normal loop.

---

## erhe Existing Infrastructure

### Graph core - reuse unchanged

`src/erhe/graph/` (`erhe::graph`): `Graph`, `Node` (pins in fixed-capacity
`etl::vector` for pointer stability), `Pin` (semantic `std::size_t` key),
`Link`. `connect()` enforces key equality + acyclicity
(`would_create_cycle`); `sort()` is topological. Fully payload-agnostic; pin
keys are unenumerated so a texture graph adds its own. Covered by
`erhe_graph_tests` (`src/erhe/graph/test/`).

### Geometry graph - the pattern to follow

`src/editor/geometry_graph/` is the proven editor-level pattern: payload
variant + pin keys (`geometry_payload.hpp`), node base with parallel payload
vectors, dirty flags, `write/read_parameters` JSON + factory type name
(`geometry_graph_node.hpp`), string factory
(`geometry_graph_node_factory.cpp`), ax::NodeEditor window with undoable edits
through `Operation_stack` (`geometry_graph_window.cpp`), four payload-agnostic
undo operations (`geometry_graph_operations.cpp`), JSON v1 serialization with
full validation (`geometry_graph_serialization.cpp`), MCP tool surface
(`mcp_server.cpp`). The async shadow-clone evaluation exists because geometry
ops are expensive; texture-graph evaluation is *string composition* (cheap),
so that machinery is **not** copied - see decisions below.

Note there is also an older, simpler `src/editor/graph/` ("Shader_graph",
`Graph_window`) - a second consumer of `erhe::graph` that proves the copy
precedent; it is unrelated to this feature despite the name.

### Graphics - everything needed exists

- **Shader from string**: `Shader_stages_create_info` (`.defines`,
  `.interface_blocks`, `.fragment_outputs`, `.no_vertex_input`, stages from
  `std::string_view`) -> `build_shader_stages()` -> `prototype.is_valid()`.
  `final_source()` injects version/defines/blocks. On-disk SPIR-V cache keyed
  by final source hash de-duplicates recompiles
  (`src/erhe/graphics/erhe_graphics/{shader_stages,spirv_cache}.hpp`).
- **Render-to-texture**: `Texture_create_info` (usage
  `color_attachment | sampled [| transfer_src]`), `Render_pass_descriptor`,
  fullscreen 3-vertex triangle from `gl_VertexID`. Working single-pass
  RTT-into-ImGui template: `src/editor/content_library/brdf_slice.cpp`
  (a `Texture_rendergraph_node` with `texture_for_gui` output); multi-pass
  chain template: `src/editor/rendergraph/post_processing.cpp`.
- **ImGui display**: `Imgui_renderer::image(Draw_texture_parameters)` takes a
  `Texture` directly (with `get_rtt_uv0/uv1` flip helpers).
- **Material binding**: assign into
  `material->data.texture_samplers.base_color.texture`; `Material_buffer`
  allocates the texture-heap handle automatically next frame. Register in
  `Content_library` (`content_library->textures->add(...)`) so browsers and
  `get_scene_textures` see it.
- **Readback + PNG**: blit texture -> host buffer (pattern in
  `src/erhe/graphics/test/gpu_test_fixture.cpp::read_texture_rgba8`), then
  `Image_writer::write_png` (fpng, headless-capable).
- **Headless GPU test suite**: `src/erhe/graphics/test/` runs a headless
  Vulkan device, fails tests on validation messages, and already has
  `draw_fullscreen_triangle(fragment_source, ...) -> pixels` - exactly the rig
  to validate composed shaders.

---

## Architecture Decisions

1. **Generation model: Material-Maker-faithful GLSL composition.** The value
   flowing through graph links is a *shader code fragment* (globals +
   uniforms + inline code + an output expression still containing `$uv`), not
   a texture. Rendering happens only at sinks: node preview thumbnails, the
   output node and buffer nodes. Rationale: the entire codegen core
   is pure string logic - unit-testable without a GPU; no intermediate
   quantization or VRAM cost; scalar parameter edits update uniforms without
   recompiling; the SPIR-V cache absorbs recompiles of unchanged sources.

2. **Fragment shader fullscreen pass, not compute.** Material Maker bakes via
   compute; erhe's `set_storage_image` is Vulkan-only while the fragment RTT
   path works on all backends and has the `brdf_slice`/`post_processing`
   precedent. Compute can be revisited for iterative buffers later.

3. **Pure codegen core is a new library `src/erhe/texgen/` (`erhe::texgen`)**
   with its own gtest suite (`src/erhe/texgen/test/`), no graphics
   dependency. It defines: the type system + conversion table, `Shader_code`,
   the substitution engine, `Node_descriptor`, and the graph composer that
   emits a complete fragment-shader body. GPU validation of composed sources
   lives in the existing `src/erhe/graphics/test/` suite (it links
   `erhe::texgen`), reusing the headless device fixture.

4. **Nodes are data: C++ descriptor tables, one generic node class.**
   Mirroring `.mmg` `shader_model`, a `Node_descriptor` holds inputs (name,
   type, default expression, function flag), outputs (type + expression),
   parameters (float/color/enum/bool/size...), global GLSL, and inline code -
   most node types are a descriptor entry with GLSL in raw string literals,
   not a bespoke class. This keeps the door open for loading
   `.mmg`-compatible JSON later without committing to it now.

5. **Editor integration copies the geometry-graph pattern**
   (`src/editor/texture_graph/`): `Texture_payload` + `Texture_pin_key`,
   `Texture_graph_node` base, factory, `Texture_graph_window`, the four undo
   operations, JSON v1 serialization, MCP tools. `erhe::graph` is shared
   unchanged. We deliberately do NOT extract shared window/evaluator templates
   up front: the geometry graph is freshly stabilized behind a 120-check smoke
   sweep, and the texture graph does not need the async shadow-clone engine
   (composition is cheap; only shader *compiles* may need async, which is a
   different mechanism). Once the texture window exists and its real shape is
   known, a dedup pass across the three `erhe::graph` consumers can be
   evaluated as separate follow-up work.

6. **Unified expression payloads.** There are no separate scalar payload
   types: a float constant node outputs type `f` with expression `0.5`; a
   color constant outputs `rgba`. Pin compatibility follows Material Maker's
   slot classes (`f`/`rgb`/`rgba` interconnect via conversion expressions).
   Types today: `f`, `rgb`, `rgba`; an `sdf2d` type would arrive with the SDF
   nodes.

7. **Parameters as uniforms.** Float/color parameters become entries in one
   std140 UBO per composed shader (respecting the project's explicit
   alignment rules); editing them updates the UBO only. Enum/bool/size
   parameters substitute code fragments/literals and therefore recompile on
   change - matching Material Maker semantics.

8. **Evaluation model: dirty-flag incremental, synchronous composition.**
   Like the geometry graph, nodes are born dirty and widget edits call
   `mark_dirty()`; evaluation walks topo order recomposing only dirty
   subtrees. Each node caches its composed `Shader_code`. Preview rendering
   and output baking are recorded during the editor frame (the window's
   render hook), never blocking on GPU readback.

---

## Verification Strategy

- **Unit tests**: `erhe_graph_tests` (the shared graph core),
  `erhe_texgen_tests` (pure string logic: every substitution rule, conversion
  insertion, global dedup, inline vs function input forms, unconnected-input
  defaults, and golden composed sources) and the composed-shader GPU tests in
  `erhe_graphics_tests` (headless Vulkan: compile, render 8x8, assert pixels,
  including a parameter-UBO case that proves a value edit needs no recompile).
  Run via ctest or the executable directly from a `build_tests*` tree, per the
  `doc/testing.md`.
- **Headless end-to-end**: `scripts/texture_graph_smoke_test.py` against the
  headless Vulkan editor build over the in-editor MCP server, including
  `texture_graph_export_png` pixel assertions and `capture_screenshot` visual
  checks. Its `NODE_SPECS` table must gain a row (pins + default parameters)
  for every new node type, which is what keeps the "all N node types present"
  check honest.
- **Descriptor self-check**: `check_texture_node_descriptors()` composes every
  descriptor standalone at `Texture_graph_window` construction and logs
  "Texture graph: all N node descriptors compose cleanly" - the cheapest
  confirmation that a newly added descriptor's GLSL substitutes and assembles.

---

## Key Files Reference

Material Maker (reference, paths relative to `<material-maker>`):

- `addons/material_maker/engine/nodes/gen_base.gd` - ShaderCode + helpers
- `addons/material_maker/engine/nodes/gen_shader.gd` - composition +
  substitution (`_get_shader_code`, `replace_variables`, `replace_input`,
  `process_parameters`)
- `addons/material_maker/engine/pipeline/compute_shader.gd` +
  `engine/nodes/buffer_compute.tres` - final shader assembly template
- `addons/material_maker/nodes/io_types.mmt` - type system
- `addons/material_maker/nodes/{perlin,blend,colorize,shape,material}.mmg` -
  representative node definitions
- `addons/material_maker/types/{gradient,curve}.gd` - widget codegen
- `addons/material_maker/engine/nodes/gen_buffer.gd` - buffer semantics

erhe (existing infrastructure):

- `src/erhe/graph/erhe_graph/{graph,node,pin,link}.hpp` - shared graph core
- `src/editor/geometry_graph/*` - the editor-level pattern being mirrored
- `src/erhe/graphics/erhe_graphics/{shader_stages,spirv_cache}.hpp` - shader
  from string + cache
- `src/editor/content_library/brdf_slice.cpp` - single-pass RTT into ImGui
- `src/editor/rendergraph/post_processing.cpp` - chained fullscreen passes
- `src/erhe/graphics/test/gpu_test_fixture.{hpp,cpp}` - headless GPU fixture
  (`draw_fullscreen_triangle`, `read_texture_rgba8`)
- `src/erhe/primitive/erhe_primitive/material.hpp` - texture samplers on
  materials
- `src/erhe/graphics/erhe_graphics/image_writer.hpp` - PNG export

erhe (this feature):

- `src/erhe/texgen/` + `src/erhe/texgen/test/` - codegen core
  (`doc/erhe/texgen.md`)
- `src/editor/texture_graph/` - editor integration
- `scripts/texture_graph_smoke_test.py` - headless verification

## Future work

- [plans/texture_graph.md](../plans/texture_graph.md) - the missing Material
  Maker families, async shader compilation, node groups and the uniform-array
  gradient / curve control points.
