# AIMD vs. erhe Debug_renderer

Comparison of AIMD ("Amelie's Immediate Mode Debug renderer", by the author of
agfx - see [agfx_comparison.md](agfx_comparison.md)) against erhe's debug
geometry renderer, `erhe::renderer::Debug_renderer` and its draw API
`Primitive_renderer` (see [erhe/renderer.md](../erhe/renderer.md) and
[erhe/debug_renderer_multiview.md](../erhe/debug_renderer_multiview.md)).
Reviewed from a local clone of AIMD at commit `4c4ad82` ("ADD: Cleanup",
2026-09) and erhe `main` on 2026-09-29. Section 5 lists what erhe could adopt
from AIMD, section 6 the reverse, section 7 erhe defects noticed during the
review.

## 1. What AIMD is

A small single-purpose library (about 1.8k lines of C/C++ and 0.9k lines of
HLSL, plus a demo) that draws debug shapes over a finished frame. It sits on
agfx (D3D12, Metal 4, Vulkan) and ships as an amalgamated single header
(`dist/aimd.h`) plus two shader files.

- **C API, ImGui-style global context.** `aimdContextCreate()` makes a context
  current; free functions (`aimdLine`, `aimdSphere`, `aimdText`, ...) append to
  it from anywhere in the frame; `aimdExecute()` draws everything once and
  clears the batches.
- **Style stack.** `aimdStyle` holds color (packed RGBA8), line thickness and
  point size (pixels), round-shape segment count, text size and flags
  (`FILLED`, `NO_DEPTH_TEST`, `ROUND_POINTS`, `TEXT_PIXEL_SIZE`, `SHADED`).
  `aimdPushStyle` / `aimdPopStyle` scope it; setters edit the top entry. The
  stack is reset to its base entry after every execute, so a missing pop does
  not leak into the next frame.
- **Three primitive kinds, two depth buckets.** Everything is tessellated on
  the CPU into lines, points or triangles, each stored as a 32-byte record
  (`aimdGpuLine`, `aimdGpuPoint`, `aimdGpuVertex`) in a `std::vector` per
  (kind, bucket). The buckets are "depth tested" and "always on top".
- **Vertex pulling, no intermediate buffers.** At execute time each vector is
  `memcpy`'d into a per-frame-in-flight CPU-to-GPU buffer (grown by 1.5x on
  demand). One non-indexed draw per (kind, bucket) reads the records through a
  bindless `StructuredBuffer` (`ResourceDescriptorHeap[PC.buffer]`); `LineVS`
  and `PointVS` expand each record to a 6-vertex screen-aligned quad in the
  vertex shader. At most 6 draw calls for the whole CPU path.
- **Pixel-exact, anti-aliased lines and points.** Thickness and point size are
  in pixels. Lines get a 1-pixel fringe and square caps (polylines join
  without gaps); the fragment shader computes analytic coverage
  `saturate(half_width + 0.5 - |distance|)`, which also fades lines thinner
  than a pixel. Round points use the same coverage against the radius. Lines
  are clipped against the near plane only, in the vertex shader.
- **Filled and shaded shapes.** With `FILLED`, closed shapes emit triangles
  with outward per-vertex normals. The fragment shader derives the face normal
  from `ddx`/`ddy` of the world position, orients it by the vertex normal and
  discards faces pointing away from the camera - back-face culling that does
  not depend on winding order. `SHADED` adds a fixed key light plus hemisphere
  ambient.
- **Depth handling.** Depth compare `LESS_EQUAL` (or `GREATER_EQUAL` with
  `reverseZ`), depth write optional (off by default). Lines and points get a
  distance-relative bias in clip space (`z` pulled toward the camera by 0.2%
  of the view distance) so wireframes over their own filled shape win; filled
  triangles get no bias.
- **Text is projected, not rendered.** `aimdText` stores a 3D anchor; execute
  projects the anchors, culls off-screen and sub-pixel labels, sorts them back
  to front and passes `aimdTextCommand` records to a user callback (the demo
  draws them with ImGui's draw list).
- **Own render pass.** `aimdExecute` begins and ends its own render pass on
  the caller's command buffer (color and optional depth, load/store), with the
  viewport fixed to `(0, 0, width, height)`. Single view (one view-projection).
- **GPU-driven mode.** User compute shaders include `AIMDDebug.hlsli` and call
  the same shape API (`renderer.DrawBox(transform)`, `DrawCone`, ...). Each
  shape reserves its slots with one `InterlockedAdd` on a per-(kind, bucket)
  counter in a single GPU geometry buffer partitioned into six regions. A
  shape that does not fit is dropped (its partially covered slots are filled
  with degenerate records) but still counted. `FinalizeCS` turns the counters
  into indirect draw commands; the same vertex shaders draw the regions with
  one indirect draw each. With `gpuAutoGrow`, the 32 bytes of counters are
  read back each frame and regions that overflowed grow by 1.5x (effective
  `framesInFlight` frames later, capped at 64 MB per region); the old buffer
  is retired until no frame in flight can use it. AIMD records every barrier
  the user's passes need. GPU-written primitives always write depth, because
  their order in the buffer is nondeterministic.
- **Stats.** `aimdGetStats` reports lines, points, triangles, labels, draw
  calls and, in GPU mode, requested counts and an overflow flag.

## 2. What erhe's Debug_renderer is

`Debug_renderer` (`src/erhe/renderer/erhe_renderer/`) is an owned object held
by the editor's app context. Callers obtain a `Primitive_renderer` with
`get(Debug_renderer_config)`; the config (primitive type, stencil reference,
draw visible / draw hidden, thin lines, x-ray) selects one of up to 32
`Debug_renderer_bucket`s, each with its own pipelines and ring buffers.

- **Two tiers.** Wide lines take the compute tier: line vertices (3 x vec4 =
  48 bytes per vertex: position + width, float RGBA color, normal + sign) are
  written straight into a mapped ring-buffer range, `compute_before_line.comp`
  expands each line into a 4-triangle "tent" (12 vertices of 48 bytes, per
  view) in a triangle SSBO, and `line_after_compute.{vert,frag}` pulls those
  triangles. Triangles, thin lines and the point bucket take the simple tier:
  the vertex buffer is drawn directly with its own topology by
  `line_simple.{vert,frag}`.
- **Frame protocol.** `begin_frame(viewport, views)`, `get(...)` + `add_*`
  calls, `compute(compute_encoder)` (caller then places the compute-to-vertex
  barrier), `render(encoder, render_pass, viewport)` inside the caller's render
  pass, `end_frame()`.
- **Visible and hidden passes.** Every bucket can draw a visible pass (depth
  `less`) and a hidden pass (depth `greater_or_equal`) that blends at a dim
  constant factor - occluded debug geometry stays readable. `xray` draws the
  hidden pass at full strength (skin bones inside a mesh). Neither pass writes
  depth.
- **Stencil layering.** Each bucket writes its `stencil_reference` with
  compare `greater` (mask `0x7f`, bit 7 is reserved for the selection
  silhouette), so a higher-priority bucket (transform gizmo handles use 3 and
  4) cannot be overdrawn by a lower one, and within a bucket the first
  fragment wins each pixel (visible pass drawn first).
- **Line widths.** Negative thickness is a constant width in logical pixels,
  multiplied by `View::pixel_scale` (display DPI; 1.0 for headset eyes);
  positive thickness is distance-scaled. Width and color are per endpoint
  (`add_line(color0, width0, p0, color1, width1, p1)`). Lines are clipped
  against the near and far planes in the compute shader. The fragment shader
  computes a round-capped distance and discards below 50% coverage (binary
  edge; smoothness comes from MSAA).
- **Surface-aligned lines.** `add_surface_lines` carries the two adjacent
  face normals of a mesh edge; the compute shader makes each half of the
  ribbon coplanar with its face and lifts it toward the viewer by
  `line_bias_margin` ULPs of the depth buffer at that depth (reverse-Z aware).
  `line_simple.vert` applies the same precision-derived bias to thin lines.
  Filled triangles use dynamic polygon offset with the sign matched to the
  depth convention.
- **Shapes with exact silhouettes.** `add_sphere` draws three great circles
  split exactly at the horizon plus the exact view silhouette circle;
  `add_cone`, `add_capsule` (tapered, common-tangent hull) and `add_torus`
  (ray-tested self-occlusion) likewise draw the true silhouette in "major"
  style and back or occluded structure lines in "minor" style. Also
  `add_cube` (with optional mid cross), `add_plane`, `add_plane_indicator`,
  `add_bone`, `add_triangle(s)`.
- **Multiview.** Constructed with `view_count >= 2`, the view UBO carries a
  camera array; the compute shader writes one triangle slab per eye and the
  multiview vertex shaders index by `gl_ViewIndex`, so one draw serves both
  headset eyes.
- **Ecosystem.** Vulkan, OpenGL and Metal through `erhe::graphics`; shader
  hot reload through the shader monitor; `Jolt_debug_renderer` adapter for
  Jolt's `DebugRenderer`; `erhe_renderer_gpu_tests` checks exact pixel line
  widths across viewport sizes, FOVs, orthographic projection and pixel
  scales. 3D labels are drawn by the separate `Text_renderer` (own font
  atlas), not by `Debug_renderer`.

## 3. Comparison table

| Aspect | AIMD | erhe `Debug_renderer` |
|---|---|---|
| Language / API shape | C API, global current context, free functions | C++ owned object, `Primitive_renderer` handle per config |
| State model | Style stack (push/pop), reset each frame | Color/thickness on `Primitive_renderer`; visibility, stencil, x-ray in `Debug_renderer_config` |
| Backends | D3D12, Metal 4, Vulkan (via agfx, bindless required) | Vulkan, OpenGL, Metal (via `erhe::graphics`) |
| Distribution | Single header + 2 shader files | Library target inside erhe |
| CPU data path | `std::vector` batches, `memcpy` to mapped buffer at execute | Written directly into mapped ring-buffer ranges (no staging copy) |
| Record size per line | 32 bytes (RGBA8 color, one width) | 96 bytes input + 576 bytes compute output per view |
| Line expansion | Vertex shader, 6 vertices, vertex pulling | Compute pass to triangle SSBO, then 12-vertex draw |
| Compute pass / barrier needed | No (CPU path) | Yes, caller places compute-to-vertex barrier |
| Draw calls | At most 6 (3 kinds x 2 buckets) + 6 indirect | Per bucket x per draw range x (visible + hidden) |
| Line width | Pixels only | Logical pixels x DPI scale, or distance-scaled; per endpoint |
| Per-endpoint color | No | Yes |
| Line anti-aliasing | Analytic coverage, 1 px fringe, sub-pixel fade | Binary (discard < 0.5); relies on MSAA |
| Line caps | Square (polyline joins) | Round |
| Clipping | Near plane (vertex shader) | Near and far planes (compute shader) |
| Points | Pixel size, square or round, AA | No point API in `Primitive_renderer`; `line_simple.vert` writes no `gl_PointSize` |
| Filled shapes | All closed shapes, winding-independent back-face cull | Triangles only (`add_triangle(s)`), flat |
| Shading | Optional key + hemisphere light | None |
| Silhouette accuracy | UV-sphere / segment tessellation | Exact silhouettes for sphere, cone, capsule, torus |
| Shape set | line, polyline, point, triangle, quad, box, AABB, frustum, cone, cylinder, arrow, axes, circle, ring, rings, sphere, grid | lines, surface lines, triangles, plane, plane indicator, cube, bone, sphere, cone, capsule, torus |
| Occluded geometry | Depth tested or always on top | Visible pass + dimmed hidden pass, optional x-ray |
| Priority between overlays | Bucket order only (on-top after depth-tested) | Stencil reference layering |
| Depth bias | Constant 0.2% of view distance (lines, points) | Depth-ULP-derived surface bias, tent coplanar with faces; polygon offset for triangles |
| Depth write | Optional (always for GPU path) | Never (stencil writes instead) |
| Blending | Straight alpha | Premultiplied alpha |
| Render pass | Own pass, viewport at (0, 0) full target | Caller's pass, arbitrary viewport rect |
| Multiview / XR | No | Yes (`gl_ViewIndex`, per-eye slabs) |
| Text | Projected anchors to user callback (UI draws) | Separate `Text_renderer` with own atlas |
| GPU-driven emission | Yes: HLSL API, atomics, indirect draws, auto-grow | No |
| Stats | `aimdGetStats` | None |
| Steady-state allocations | None once vectors reach high water (capacity kept) | None once buckets reach high water (draw entries, view spans and span views live in flat vectors cleared with capacity kept) |
| Shader hot reload | No | Yes (shader monitor) |
| Tests | Demo only | GPU pixel-width test (`erhe_renderer_gpu_tests`) |
| Physics integration | None | Jolt `DebugRenderer` adapter |

## 4. Strengths and weaknesses

### 4.1 AIMD

Strengths:

- **Lean data path.** 32 bytes per line, expanded in the vertex shader from a
  bindless buffer: no intermediate buffer, no compute pass, no barrier, and a
  constant handful of draw calls regardless of how many call sites drew.
- **Visual quality for pixel-sized primitives.** Analytic coverage gives
  smooth line and point edges without MSAA and degrades gracefully below one
  pixel.
- **GPU-driven emission.** The standout feature: compute shaders call the same
  shape API as the CPU and the result is drawn with no readback on the draw
  path. Overflow is handled safely (drop plus counted demand), growth is
  automatic and retirement of the old buffer is frame-safe. AIMD owns the
  barriers, so users cannot get synchronization wrong.
- **Filled / shaded shapes with winding-independent culling.** Useful for
  volumes (bounds, colliders, light cones) where outlines alone read poorly.
- **Ergonomics.** Style stack, `AIMD_RGBA` packed colors, `aimdTextf`, stats,
  a one-file integration; the text callback keeps glyph rendering out of the
  library.

Weaknesses:

- **Single view, own pass, full-target viewport.** No sub-viewport offset
  (split or four-view layouts), no stereo, and an extra render pass with
  load/store of color and depth (costly on tiled GPUs such as Quest).
- **Depth-fighting heuristics.** The line bias is a fixed fraction of view
  distance, unrelated to depth precision; filled triangles have no bias, so a
  filled overlay coplanar with scene geometry z-fights.
- **No occluded-geometry view and no priority layering.** A shape is either
  depth tested (vanishes when occluded) or always on top.
- **Only pixel widths, one color per line.** No world-space thickness, no
  gradients, no DPI scale factor in the API.
- **Global mutable state.** The current-context pointer and unsynchronized
  vectors make multi-threaded emission impossible without external locking.
- **Double copy on the CPU path** (vector push, then `memcpy` at execute).
- **GPU path limits.** Fixed region partitions, one frame-in-flight latency
  before growth takes effect, translucent GPU shapes blend in nondeterministic
  order, and everything depends on SM 6.6-style `ResourceDescriptorHeap`.
- **No tests, no hot reload;** correctness is demonstrated by the demo only.

### 4.2 erhe Debug_renderer

Strengths:

- **Editor-grade depth semantics.** Visible + dimmed hidden passes, x-ray,
  and stencil-reference layering let gizmos, bones and helpers coexist and
  stay legible behind geometry.
- **Principled surface lines.** Bias derived from depth-buffer ULPs and the
  coplanar tent, rather than a tuned constant; correct for forward and
  reverse Z.
- **Exact silhouettes.** Spheres, cones, capsules and tori read as the real
  shape from any viewpoint with far fewer segments than a dense wireframe.
- **Flexible line widths.** DPI-aware logical pixels or distance-scaled
  widths, per-endpoint width and color, verified by an automated GPU test.
- **Integrates with the frame.** Draws inside the caller's render pass and
  viewport rect, supports multiview for the headset, runs on GL/Vulkan/Metal,
  hot-reloads its shaders.
- **Zero-copy CPU writes** into mapped ring-buffer ranges.

Weaknesses:

- **Heavy wide-line path.** Each line costs 96 bytes in plus 576 bytes per view
  written by compute and read back twice (visible and hidden passes). It needs
  a compute pass, a caller-placed barrier and a workgroup-count / tail-guard
  contract between `dispatch_compute` and the shader. The tent branch also computes `transpose(inverse(clip_from_world))`
  per line thread for surface lines.
- **Binary line edges.** Without MSAA, wide lines alias; there is no
  sub-pixel fade for very thin lines.
- **Feature gaps.** No point API, no filled closed shapes, no shading, no
  arrow / axes / frustum / cylinder / circle / grid helpers in the library
  (the editor draws some of these itself), no stats, no GPU-side emission.
- **API ergonomics.** Many positional parameters per shape (major/minor color
  and thickness, camera position, step counts); state is split between the
  config and the `Primitive_renderer`; float RGBA colors (16 bytes) per vertex.
## 5. What erhe could borrow from AIMD

Ordered by value to erhe.

1. **GPU-driven debug emission.** A GLSL include (the counterpart of
   `AIMDDebug.hlsli`) with an atomic slot reservation into region-partitioned
   buffers, a finalize compute that writes indirect draw commands, overflow
   counting with dropped-shape padding, and optional counter readback for
   growth. erhe's GPU systems would use it directly: DDGI and radiance-cascade
   probes, shadow-fit volumes, culling results, BVH nodes. The radiance
   cascades probe overlay reads a copy of the probe data back to the CPU
   (every `c_probe_overlay_interval_frames` frames) to draw it; GPU emission
   removes that readback and its latency. Draw the output through the same
   visible/hidden/stencil buckets so it inherits erhe's depth semantics.
2. **Vertex-shader expansion with vertex pulling instead of the compute
   tier.** Pull the line record from the input SSBO by
   `gl_VertexIndex / vertices_per_line` and compute the corner in the vertex
   shader. The 4-triangle tent maps to 12 vertices per line the same way (each
   invocation recomputes the per-line setup; ALU is cheaper than the 576-byte
   round trip). This removes the compute pass, the triangle SSBO, the
   caller-placed barrier and the per-view slab stride (the multiview vertex
   shader already runs per view). Move the clip-space plane matrix
   (`transpose(inverse(clip_from_world))`) into the view UBO, computed once on
   the CPU.
3. **Analytic anti-aliasing.** Extend each ribbon by a 1-pixel fringe and
   output coverage instead of `discard` at 0.5; fade lines below one pixel.
   Premultiplied blending already matches coverage-scaled alpha. Keep the
   discard path for the stencil-layered buckets where partial coverage would
   still claim the stencil (or write stencil only above a coverage threshold).
4. **Compact records for the common case.** One 32-byte record per line
   (positions, RGBA8 color, one width) alongside the current per-endpoint
   layout used for gradients and surface lines; RGBA8 colors in the triangle
   path.
5. **Points.** A `Primitive_renderer::add_points` with pixel size and
   square/round shape, expanded as quads (never API points, whose size
   support differs across backends).
6. **Filled closed shapes with winding-independent culling and optional
   shading** - outward per-vertex normals plus the derivative face normal, as
   AIMD does, so filled bounds, colliders and light volumes need no winding
   discipline.
7. **Stats.** Per-frame counts of lines, triangles, draw calls and buckets,
   exposed to the performance window and the MCP server.
8. **Scoped style.** An RAII style scope on `Primitive_renderer` (color,
   thickness, segment count) and a `Debug_style` value with named fields, so
   shape calls stop taking six positional style parameters.
9. **Shape helpers.** Arrow, axes, frustum from an inverse view-projection,
   cylinder, circle / ring, grid and polyline in the library, replacing the
   ad-hoc versions in editor code.
10. **3D labels on the debug API.** `add_label(position, text)` forwarding to
    `Text_renderer` so debug code has one entry point; AIMD's culling of
    off-screen and sub-pixel labels applies unchanged.

## 6. What AIMD could borrow from erhe

1. **Hidden pass and x-ray.** A dimmed second pass with the inverted depth
   test shows occluded debug geometry without making it always-on-top.
2. **Priority layering via stencil.** Lets gizmo-like overlays win over
   ordinary debug geometry independent of submission order.
3. **Precision-derived depth bias.** Derive the line lift from the depth
   format's ULP at the fragment depth (reverse-Z aware) instead of a fixed
   0.2% of distance, and apply polygon offset to filled triangles so overlays
   coplanar with scene surfaces do not z-fight. The surface "tent" (per-side
   coplanar ribbon) solves wide mesh-edge overlays.
4. **Draw into the caller's pass and viewport.** An execute variant that
   records into an open render pass with a viewport rect supports split views
   and avoids the extra load/store pass on tiled GPUs.
5. **Multiview.** A view-projection array indexed by view ID in the vertex
   shader serves stereo with one draw.
6. **Width model.** DPI-aware logical-pixel widths and a world-space width
   option; per-endpoint width and color.
7. **Exact silhouettes** for spheres, cones, capsules and tori, which read far
   better than UV wireframes at low segment counts.
8. **Zero-copy CPU path.** Reserve space in the mapped upload buffer and write
   records in place instead of pushing to a vector and copying at execute.
9. **Automated pixel tests.** An offscreen test that measures line widths and
   coverage across viewport sizes, projections and scales.

## 7. erhe defects noticed during the review

- The point bucket is reachable through `Debug_renderer_config` but
  `line_simple.vert` never writes `gl_PointSize`; Vulkan (without
  `maintenance5`) leaves the point size undefined in that case and Metal
  requires `[[point_size]]`, so point topology draws are not portable.
