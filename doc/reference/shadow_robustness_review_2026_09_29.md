# Shadow robustness: change summary and review (2026-09-29)

Analytic review of the shadow robustness series, commits `afdef669b..a928e3a3b`.
The current design is in [shadows.md](../erhe/shadows.md) and
[point_light_shadows.md](../erhe/point_light_shadows.md); remaining work is in
[plans/shadow_robustness.md](../plans/shadow_robustness.md).

## Scope and method

The review covers the 36 commits after `afdef669b` (`34f193768` onward) that
harden erhe's directional, spot and point shadow maps. The trigger was a dark
stripe in `gi_cornell.glb`: a spot light aimed straight down at the floor,
where stored and reference depth tied to one float ulp and every bias term was
zero.

- Reviewer: a read-only agent that read the commits, the landed shaders and
  C++, `shadows.md`, `point_light_shadows.md` and the remaining plan.
- Method: analytic only. Each derived bound is checked against its
  implementation and its assumptions are named. Nothing was built, run or
  re-measured for this review.
- Measured state at review time: the final gate (Low, Medium, High, distance
  technique, depth clamp off) has 0 failures; `cull_front` failures are known
  and documented; T7 passes 30/30 on Vulkan and OpenGL; T8 passes; the forward
  pass costs +12 % median (accepted).
- Not covered: Metal (needs a macOS machine), runtime profiling of the +12 %.

## Summary of changes

The series fixes four existing shadow defects. Every bias is now derived from
bounds on real error sources, with no tuned constants. It also adds an analytic
verification harness. In total: +9480 / -632 lines over 85 files. The largest
pieces are `doc/erhe/shadows.md` (+1162), the new `scripts/shadow_verify.py`
(2371), `res/shaders/erhe_light.glsl` (+780) and the T7 GPU test fixture
(~1330).

| Area | What changed | Commits |
| --- | --- | --- |
| Tooling | `set_graphics_preset` MCP tool (session-only); `ERHE_FORCE_DISABLE_REVERSE_DEPTH`; world-position (36) and per-light shadow-visibility (30) debug modes; `render_scene_image` fp32 capture, NaN background marker and per-light shadow matrices; forward-pass GPU timer via `get_gpu_timers` | 0455be972, b5ca7f85b, 85c75bf78, c853d1279 |
| Test stations and gates | 7 box-only stations plus Cornell; `shadow_verify.py` with G1-G7 against slab-cast ground truth; exact per-face edge band; pairwise covering matrix; `--extra-light`, `--root-offset`, `--g7` | b8e73afe4, 257cff784, 2750ea461, 502f9636b, 141da0582, 5b8d03abf |
| GPU / MCP tests | T7: plane tie swept -4..+4 ulps over poses, depth formats, filters and both techniques (30/30 Vulkan and OpenGL); T8: head-on acne on `head_on_floor` and Cornell | 37c43fd20, 46abc64be, 56dce4e61 |
| Defects fixed | D0: variant built for the requested 24 bits while the map was D32F. D8: forward-Z rasterizer bias sign. Border narrower than the filter reach. Exact texel selection (a gather coordinate at 511/512) | 5e10ed9d0, 5711fe633, 720f4549a, fefe0b014 |
| Bias design | D4: slope scale 1 to the fetched texel centre plus a caster snap term. D2/D3: plane-derived depth gradient, grazing clamp 0.05. D1: minimum bias = projection + position + raster + gradient + format. D9: raster bound under depth clamp (directional only). D11: spot distance-technique resolution floor with per-light fallback | 28bf2cd60, 061293535, 154627477, c0b72caa8, d9bb299ad |
| Point lights (D6) | Cube caster stores its plane's radial distance on the texel-centre ray; derived receiver bias replaces `max(0.05, 0.02 d)`; minimum 64 texels per face | 96048e304 |
| Distance technique (D7, D10) | Spot and directional store caster-plane distances on `precise` texel rays; farther-of-plane-and-point rule for convex creases; fwidth bias removed | 95d58c055 |
| Cull mode (D5) | `cull_back` default everywhere; `cull_front` documented as leaking at contacts | 171f5e9ba |
| View-relative positions (D12) | `standard.vert` subtracts the view origin from the node translation (`precise`); relative matrices composed in double per view and per light; holds from 10 m to 10 km with no per-primitive per-frame work | bfda892b4 |
| Backends and cost | OpenGL verified (reverse-Z); `precise` gated on GLSL 4.00. G7 forward pass +12 % median, accepted | b880a2ffd, a928e3a3b |

Presets: Medium and High `shadow_depth_bias_slope` goes from -1 to 0.

## Quality and correctness analysis

The geometric parts are sound and in places exact. The weak points are where a
bound rests on an unverified hardware or compiler assumption, and one
derivation, position rounding, uses the wrong magnitude.

| Design element | Verdict | Relies on |
| --- | --- | --- |
| D2/D3 plane gradient `transpose(W) * (N, -N.P)`, `dz/du = -a/c` | Sound and exact for planes. The error bound is an exact rearrangement, not first order. The grazing clamp does not amplify normal error | Geometric normal from `dFdxFine`, exact for planar triangles |
| D4 tap offsets and exact texel selection | Sound. Scale 1 is exact for a planar receiver. Fetching at centres or shared corners is robust to any sub-texel precision | Nothing device-specific |
| Border and fit margin `B + reach - 0.5` | Tight, not loose: the first wide-path tap lands on the first non-border texel. One snap-slack texel is enough | Fit snapping moves the corner by less than one texel |
| Caster vertex snap `2^-8 (abs(dz/du) + abs(dz/dv)) / N` | Form derived, step assumed: 1/256 is hard-coded in 4 places. Measured margin only 2x (2^-9 passes, 2^-10 fails) | 8 sub-pixel bits; Vulkan guarantees only 4 |
| D1 projection term | Mostly sound. Counts the shader divide as 0.5 ulp, but Vulkan allows 2.5 ULP (`point_light_shadows.md` uses 2.5). The caster divide is fixed-function and unspecified | Divide precision; the raster floor usually dominates |
| D1 position term | Weakest derivation. It scales with the view-relative `abs(P)`, but the fp32 error of `M * node_position` scales with the vertex's distance from its node origin (`erhe_light.glsl:88-98`, `standard.vert:209-227`) | Small meshes near their node origin, which every station is |
| D1 raster term `4u * Z`, Z under depth clamp (D9) | Right model; right fix for near-clipped casters. Under clamp, Z comes from all visible casters, not those in the light footprint | Fixed-function interpolation rounds once per step; measured on one AMD iGPU |
| Undetermined plane | Correct coverage. The fallback (`19.97 = tan(acos 0.05)`) is about 10 texels of bias, a graceful degradation | Only at fp32 resolution limits |
| D7 distance technique | Correct if the caster and receiver rays are bit-identical. `precise` (NoContraction) does not pin `normalize` or divide precision, so bit equality is driver behaviour, not a spec guarantee. D11 floor derived; the farther-of-two rule provably cannot shadow a lit receiver | Same driver compiling both shaders the same way |
| D6 point lights | Radial-derivative snap term verified. The 0.018 rad validity (caster plane gate 0.01) is not checked by the receiver | Normal errors well under the margin |
| D5 cull mode, depth clamp, reference clamp | Sound. `cull_front` leak explained (99.4 % of failing pixels reproduced by a tap trace). Depth clamp semantics match the Vulkan spec; restricting it to directional lights is correct | - |
| D12 view-relative positions | Sound. The difference of two exact fp32 values is correctly rounded, so no hi/lo split is needed. A multiview pass shares the first view's origin, harmless for eye separation | `precise` propagation through the temporary |

Derived: the gradient and offset identities, the margin widths, the D11 floor
and the snap-bias form.

Empirical or assumed:

- the 1/256 snap step;
- the fp32 raster interpolation model;
- the 0.05 grazing limit (a scope choice) and from it 19.97 and 0.025;
- the 0.01 caster plane gates and the 0.018 rad margin;
- the gate thresholds (G1/G2 0.999/0.001, G3 1.5 texels, G5);
- the +12 % / +16 % G7 acceptance, measured on one machine in a Debug build.

## Potential issues

One issue is high severity: large meshes far from their node origin are not
covered by the position bound. None of these issues is exercised by the current
test stations.

| # | Severity | Location | Issue | Failure scenario |
| --- | --- | --- | --- | --- |
| 1 | High | `erhe_light.glsl:88-98` (used at :584, :430, :863); `standard.vert:209-227` | Position rounding bound scales with view-relative `abs(P)`; the fp32 error of `M * node_position` scales with the vertex's distance from its node origin | A 500 m ground mesh with its node at one corner, camera 2 m above: about 0.25 mm of rounding against a bound of micrometres, so acne far from the node origin |
| 2 | Medium | `light_buffer.cpp:319-331, 511` | Depth-clamp raster bound Z taken over all visible casters, not those in the light footprint | A caster 1 km above the fitted near plane gives about 1 cm of bias on a 50 m range; contact shadows detach (G3) |
| 3 | Medium | `erhe_light.glsl:394, 526, 854`; `light_buffer.cpp:348` | Hard-coded 1/256 vertex snap; spec minimum is 4 sub-pixel bits; measured margin only 2x | A device with `subPixelPrecisionBits = 4` shows acne on grazing surfaces with the hard filter |
| 4 | Medium | `erhe_light.glsl:582, 245-249` | Divide bounded at 0.5 ulp; Vulkan allows 2.5 ULP; inconsistent with `point_light_shadows.md` | Forward-Z far receivers (z near 1) on a driver with a multi-ulp reciprocal: uncovered last-bit ties |
| 5 | Medium | D5 default `cull_back` (171f5e9ba) | One-sided geometry facing away from the light casts nothing | A glTF ceiling quad with `doubleSided=false` and its normal pointing down: the room below loses its shadow silently |
| 6 | Medium | `erhe_shadow_distance.glsl:58-66` vs `standard.frag:308-319` | Bit-identical caster and receiver rays is a driver property, not a spec guarantee | A driver compiles `normalize()` differently in the two shaders, or MSL fast-math on Metal: systematic unbounded ties |
| 7 | Low | `standard.frag:379`, `erhe_point_shadow.glsl:16` | Receiver does not check the caster's 0.01 plane gate / 0.018 rad validity | Grazing receiver far from a point light stores `length(p)` instead of its plane distance: acne |
| 8 | Low | `erhe_light.glsl:591` | Undetermined-plane fallback is about 20x the half-texel lateral | Tens of cm of bias at extreme distances; leaks under casters |
| 9 | Low | `light.cpp:418` (`perspective_z_near = 0.04f // TODO`) | Spot casters within 4 cm are clipped silently | A caster touching the spot light casts nothing (out of scope, not surfaced in the UI) |
| 10 | Low | GL `[-1,1]` depth range | Viewport depth-transform rounding is not in the projection term; GL verified under reverse-Z only | GL forward-Z far receivers |
| 11 | Low | Docs and code | Drift: `erhe_light.glsl:85` says the CPU composes the translation (true before D12); `shadows.md` calls the `depth_clamp` default off while committed settings and verification run clamped; `shadow_bias_origin_scale` also scales projection and raster | Misleading maintenance |

## Improvements

Fix the bound issues first, because they are correctness; performance work
next can recover much of the +12 % by moving per-light constants out of the
fragment shader.

1. Bound fixes (issues 1-4).
    - Add a node-origin term to the vertex rounding. The primitive's AABB
      extent is already in `position_scale`, so this needs no new per-frame
      data.
    - Restrict D9's Z to casters inside the light's footprint; the fit already
      culls them.
    - Read the sub-pixel snap step from the device limit into the light block
      instead of hard-coding 1/256.
    - Bound the divide at 2.5 ULP, or account one ulp for a `precise` divide on
      both sides.
2. Performance (G7).
    - Per-light constants recomputed per fragment move to the light block:
      `abs()` of the z/w rows (`erhe_light.glsl:578-579`); for directional
      lights `D_u`, `D_v`, `D`, their lengths and the depth axis (:354-358).
    - Per-fragment work repeated per light is computed once:
      `get_position_rounding()` and `length(origin_offset)`.
    - Replace `transpose(W) * plane` (:491, 16 MADs) with the dot products
      already computed.
    - Distance technique: `get_shadow_distance_tap()` does a mat4 multiply and
      a `normalize()` per tap (36 per pcf_6x6 lookup). For directional lights
      the direction is constant and the origin affine in the texel index.
    - The distance variant compiles both techniques because of D11; compile
      the depth fallback only when a spot light needs it.
3. Maintainability. `grazing_cos = 0.05` appears in 4 places, the snap step in
   4, `plane_cos_min = 0.01` in 2; the grazing clamp plus `plane_determined`
   block is copied verbatim (:361-375, :818-827). Move the constants to one
   include and the clamp to a function.
4. Test coverage gaps. Add stations and cases for:
    - a single large mesh with a far node origin (issue 1);
    - casters outside the footprint under depth clamp (issue 2);
    - one-sided casters (issue 5);
    - MSAA on;
    - skinned and alpha-tested casters;
    - multiview / XR;
    - GL forward-Z;
    - Metal.

   T7 uses 4 poses on one device; T8 pins Medium only; G7 was measured on one
   AMD iGPU in a Debug build.
5. API and docs consistency.
    - Rename the two bias scales by what they scale.
    - Fix the stale comment at `erhe_light.glsl:85`.
    - State the `depth_clamp` default the verification actually runs.
    - Record in `shadows.md` that the 8-bit snap, the raster model and the
      divide precision are device assumptions, with their measured margins, so
      a failure on other hardware is diagnosable.
