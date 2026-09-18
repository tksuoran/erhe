# Texture graph backlog

Status: proposed

This plan extends `doc/editor/texture_graph.md`, which describes the editor's
procedural texture graph as it is, and `doc/erhe/texgen.md`, which describes
its codegen core. Everything below is outstanding; the node-coverage tables in
`doc/editor/texture_graph.md` stay the single record of what is ported and what is
deliberately left out.

## Node families, in the order they pay off

Take the families from the family table in `doc/editor/texture_graph.md` (each row
carries a cost, benefit and score estimate), highest score first among the ones
marked missing:

1. **Image / texture input** (score 2.5) - sample an external bitmap as a graph
   source. The `buffer` node already proves that a `sampler2D`-backed
   expression works downstream, so this is mostly asset plumbing.
2. **Transform / UV warps, remaining 12 of ~25** (2.5) - `multi_warp`
   (compound node), `distort` (needs a lattice widget parameter kind),
   `custom_uv` (tileset + variation machinery), `warp_dilation*` (multi-pass
   buffers). `twist` is sdf3d and out of scope.
3. **2D SDF** (1.3) - an `sdf2d` value type plus shape, boolean, stroke and
   fill nodes (~51 Material Maker nodes). The new value type comes first, then
   many tiny nodes; it buys crisp resolution-independent shape authoring.
4. **Height / normal / AO** (1.3) - `normal2height`, `normal_blend`,
   `occlusion`, `hbao`, `slope`, `smooth_curvature` and the rest. Key for PBR
   authoring; several need buffer nodes, which exist.
5. **Brick / weave variants** (1.0), **tiling / splatter** (1.0), **bit
   packing** (1.0), **image-processing filters** (0.75).
6. **Node groups** (0.75) - reuse the geometry graph's Group pattern; the
   payoff grows with the library size.
7. **Fill family** (0.6) - blocked on iterate-buffer machinery
   (`gen_iterate_buffer.gd`), which erhe does not have. `cairo`'s and
   `voronoi`'s `fill` outputs are blocked on the same thing.

Two smaller items in the same area:

- **`fbm` basis expansion**: Material Maker's `fbm2`, `fbm3` and `fbm4` are the
  same node with larger basis libraries (simplex, cellular3..8, voronoise,
  gabor). Append those bases to the existing `fbm` node's enum rather than
  adding three near-duplicate nodes - appending keeps the existing indices, so
  saved graphs stay valid.
- **Point-list parameter widget**: Material Maker's `splines` and `polycurve`
  are driven by a point-list parameter whose GLSL is generated per instance
  from the edited points. erhe has no such `Parameter_kind`; adding one is a
  widget plus parameter-codegen feature, and it unblocks both nodes.

## Async shader compilation

Composition is synchronous and cheap (`doc/editor/texture_graph.md` decision 8) and
stays that way, but shader *compilation* is not: a graph edit that changes the
composed source blocks the editor frame on `build_shader_stages`. Move the
compile onto the existing `tf::Executor` - compose on the main thread, compile
off-thread, swap the pipeline in when it is ready, and keep showing the stale
preview meanwhile. A performance pass over preview throttling and compile
dedup metrics belongs with it.

## Uniform-array gradient and curve control points

`erhe::texgen` bakes gradient and curve control points into the emitted helper
function as GLSL constants, so any value edit recomposes and recompiles (the
SPIR-V cache absorbs unchanged sources; see the DECISION note in
`doc/erhe/texgen.md`). Emitting them as std140 uniform-array members instead,
the way float and color parameters already are, would let a value edit skip the
recompile; a structural edit (adding or removing a stop) would still recompile.
It costs a std140 array-uniform layout and a per-frame upload path, which is
why it is not done.

## Composition performance

Both items in the "Performance notes / Known limitations" section of
`doc/erhe/texgen.md` are measured-first candidates, not commitments: the
O(d^2) `Shader_code` merge (thread a single accumulator through the recursion
with hashed dedup indexes) and the O(n*k) right-to-left `replace_variables`
shuffle (a segment-and-join rewrite must preserve the exact re-scan semantics,
which are load-bearing). Templates are short and neither is a measured hot
path today.

## A home for the bake driver

`Texture_graph_window::update()` is the per-frame driver that evaluates and
bakes every `Graph_texture` in every scene, and the window owns the shared
`Texture_renderer` (`doc/editor/graph_texture.md`). Neither belongs to a window: they
run whether or not the window is open. Move both into a dedicated non-window
part.

## Stretch

Import a subset of `.mmg` node definitions directly. The descriptor model was
kept data-shaped (`doc/editor/texture_graph.md` decision 4) so this stays possible.
