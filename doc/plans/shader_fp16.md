# Shader fp16 paths: incremental half precision in the standard shader

Status: in progress

This plan extends `doc/erhe/shader_variants.md` (the `Shader_key` axes and
`erhe_standard_variant.glsl`) with a set of independently switchable fp16
paths through `standard.frag` and `standard.vert`, each with a stated
boundary inside which its results are acceptable, a test that checks the
boundary, and a measurement of what it buys on a Mali core
(`doc/erhe/graphics.md` "Mali analytics"). The first target is low-end
devices: one light at most, the cheapest shadow mapping (hard filter, depth
technique), no anisotropic material, no DDGI or radiance cascades.

## Status and first results (2026-10-29)

Step 0 and the GLSL of P1 - P5 are built; the first measurements follow.

Built: the `FP16_PATHS` axis and `Fp16_path` bits, the graphics preset
field `shader_fp16_paths` (Settings window checkboxes,
`set_graphics_preset shader_fp16_paths`, applied through the same sites as
the specular anti-aliasing axis; the plan's separate setting and MCP tools
became this preset field), `res/shaders/erhe_fp16_paths.glsl` with the
per-path type macros, the GLSL of P1 - P5, the float controls query
(`Device_info::shader_denorm_*_float16` and the startup log line),
`creation_26_fp16_test_rooms.py` stations A - E (F waits for P6),
`scripts/shader_fp16_verify.py` (commands in `doc/testing.md` "Shader
fp16 verification"), and the unit tests `Shader_key_fp16_paths.*`. The fp32
chain of every variant is the original code operation for operation (mask
0 renders byte-identically to itself; the self row of every case is zero).

Measured on the development iGPU (`shaderFloat16` on; the float controls
report denormal preserve and flush-to-zero both available, and the
driver's default flushes fp16 denormals to zero: products below 2^-14 read
back as 0), `--matrix chains` over the five stations, 40 gated cases,
under the first run's ulp gate (G-display results in "Gate scale"
below):

- **P5 visibility**: every case bit-identical to fp32 (hard filter; the
  factor is exact). Passes.
- **P1 material**: 38/40; the two failures are the highlight peaks of the
  roughness 0.09 and 0.13 spheres, relative error 1.4e-3, from the fp16
  rounding of roughness (2^-11) amplified by the distribution's
  roughness^-4 at the peak. Elsewhere exact.
- **P2 BRDF**, three forms measured: (1) everything in fp16 including the
  vectors: 13/40, highlight errors 13 - 76% (the 2^-11 angular error of
  fp16 unit vectors at the flank of a sharp lobe), grazing silhouettes 73%.
  (2) fp32 vectors, fp16 dot products and lobe math: the sharp peaks still
  75% high because sin^2 of the half angle (below 2^-14 near the peak)
  flushes to zero in fp16. (3) Form 2 with the distribution's denominator
  normalized by alpha^2 (what the shader now holds): worst relative error
  4.8e-3 on the sphere stations, 2.8e-3 on the grazing sphere, 1.6 - 3.2e-3
  on the rough floor; the error is uniform fp16 rounding over about ten
  operations (3 - 10 ulps), no longer concentrated anywhere. The boundary
  of form 3 is the peak value 1 / (pi alpha^2) <= 65504, roughness >= 0.064;
  the P1 floor 0.09 keeps a margin.
- **P3 accumulation**: 31/40. The failures are the measured boundaries:
  radiance x exposure >= 65504 overflows to infinity (clamped to 65504 at
  the output; the 2^12 x 2^4 and 2^14 x 2^4 cases), and light terms below
  2^-14 flush to zero on this driver (the dark flank pixels of the sphere
  stations, absolute error up to 1.8e-4). Within the boundary the error is
  1 - 2 ulps. The exposure folding keeps the dark room (ambient 2^-10 on
  base colors down to 2^-6) within the gates at exposures 1 and 16.
- **P4 attenuation**: 35/40 alone and no gain: the inverse square term
  underflows the fp16 normal range beyond 128 m (the 100 m and 500 m point
  lights go black past that distance), so it must stay fp32, which leaves
  only the window polynomial in fp16 (no register or cycle change in the
  analytics). Kept by user decision (see "P4 kept" below).
- **G-cost (Mali-G310, the recorded variants of the run)**: the isotropic
  one-light variant (hard filter, 16-bit depth, no textures) holds 64 work
  registers at 50% occupancy under every mask; the fp16 arithmetic share
  reaches 20% with P2 and the cycle counts move by less than 2% (the
  conversions). The register pressure of this variant is not in the
  shading chain P1 - P5 covers; the all-half spirv-opt experiment reached
  32 registers only because it also converted the shadow lookup, the
  receiver plane and the position math, which this plan keeps fp32 by
  design. The register budget below locates the pressure and gives the
  next steps (R1 - R3).
- **Gate scale** (decided 2026-10-29): the 2^-10 ulp gate of the first run
  is below what any fp16 chain produces and below what the fp32 reference
  itself reproduces at the sharpest highlight flanks (1 - N.H^2 cancels
  there and fp32 contraction differences move the result by about 1e-3).
  The gate is now G-display: 4/255 on the 8-bit sRGB display value for
  99.9% of pixels, 16/255 for every pixel. With it, `--matrix chains`
  gives: P1 38/38 (display delta at most 1/255), P3 38/38 (1/255 inside
  its boundaries), P5 38/38 (0), P4 35/38 (5/255; the 100 m and 500 m
  point lights are ungated as outside the path's boundary), P2 36/38
  (22/255). The remaining failures are single pixels that go to zero
  where the reference is 0.001 - 0.008 (G-dark): the silhouette pixels of
  the roughness 0.09 red metal sphere under P2, where the specular tail's
  factors fall below 2^-14 and flush, and one outermost cone-edge pixel
  of each spot light under P4. fp16's range cannot hold the peak of a
  roughness 0.09 lobe (4815) and its silhouette tail (below 2^-14)
  together, so the P2 boundary for content is a roughness floor above
  0.09 on sharp metals or acceptance of isolated black silhouette pixels.
- **P4 kept** (decided 2026-10-29) with its boundary as a content
  constraint: receivers within 128 m of a point or spot light (the fp16
  inverse square underflow); the test rooms mark farther cases ungated.
- **Texture coordinates** (for P8, decided 2026-10-29): the acceptance
  there is that an fp16 texcoord selects a texel in the immediate
  neighbourhood of the fp32 one, which bounds fp16 texcoords to textures
  of at most 2048 texels per axis (the plan's P8 statement).

Open for the user: whether P1 keeps roughness in fp32 (its remaining
peak rounding is 1/255, within the gate as it stands).

### Register budget of the one-light variant (2026-10-29)

Measured offline on Mali-G310 (the recorded variant's preamble plus the
shader sources compiled with glslang and spirv-opt -O, then malioc; the
baseline reproduces the 64 registers of the editor's build), each row one
change to the isotropic, vertex-colored, one shadowed directional light,
hard filter, 32-bit depth variant:

| Variant | Registers | Occupancy | Cycles |
|---|---|---|---|
| as built (any fp16 mask) | 64 | 50% | 11.4 |
| no light | 15 | 100% | 0.6 |
| one unshadowed light (any fp16 mask) | 32 | 100% | 3.9 |
| shadowed, visibility forced to 1 | 32 | 100% | 3.9 |
| shadowed, naive compare (no bias, no plane) | 32 | 100% | 4.8 |
| shadowed, receiver plane off | 61 | 50% | 8.1 |
| shadowed, minimum bias off | 54 | 50% | 9.2 |
| shadowed, both off | 32 | 100% | 6.3 |
| shadowed, per-light orthographic constants (D_u, D_v, D, plane rows, row magnitudes precomputed) | 58 | 50% | 10.0 |
| unshadowed, Lambert only (no Fresnel, no specular) | 23 | 100% | 1.3 |
| shadowed, Lambert only, full bias | 32 | 100% | 8.9 |
| visibility phase before the material and BRDF phase | 64 | 50% | 11.4 |
| visibility phase first + per-light orthographic constants | 32 | 100% | 9.9 |
| the same with fp16 paths 7 | 32 | 100% | 10.0 |
| the same with PCF 4x4 | 64 | 50% | 13.7 |
| spot light as built / without plane and minimum bias | 64 / 61 | 50% | 12.5 / 7.3 |

Reading: the lit path with the full BRDF sits at the 32-register edge on
its own (the Fresnel and specular chain is nine of them: Lambert only is
23), and the derived shadow bias of doc/erhe/shadows.md (receiver plane
and minimum bias, D1 - D12) needs about as much again; either alone
over the other's live state crosses into the 64-register allocation. The
fp16 paths change none of these rows, because what they shrink is a few
scalars and one color. Two structural changes together bring the
directional one-light hard-filter variant to 32 registers at full
occupancy and 13% fewer cycles, with the bias intact:

1. **Two phases per fragment**: the shadow visibility of every light is
   computed first, right after the receiver plane and before the material
   fetches and the BRDF (one float per light kept), so the lookup's peak
   does not sit on top of the shading state and the shading peak not on
   top of the lookup's. For the loop path this is a visibility array of
   the key's light count; the clustered path keeps its single loop.
2. **Per-light constants for orthographic lights**: for a directional
   light D_u, D_v, D, |D|, the transposed plane rows and the projection
   row magnitudes do not depend on the receiver point, so the light record
   carries them precomputed (four vec4) and the lookup reads them instead
   of forming them from the two 4x4 matrices per fragment.

Each alone stays at 64. Spot and point lights are perspective, so change
2 does not apply to them and their lookup starts 29 registers above the
budget even without the plane and the bias; they need the
precision-neutral reformulation of the bias terms (fewer vector
temporaries) before the budget is reachable, which is a separate
investigation. PCF 4x4 does not fit either way: the gather kernel adds
its taps on top. The low-end configuration of this plan (one directional
light, hard filter) is exactly the case the two changes cover.

## Goals

- Reduce the lit variants' work registers below the 64 that every one of
  them uses today (half occupancy on Valhall), and remove their spills, by
  computing the shading chain in `float16_t` where the result is
  indistinguishable at the fp16 render target.
- Every fp16 path is a switch: off by default until its gate passes, on by
  itself or with the others, so a regression is bisected by switching paths
  and the fp32 chain stays the reference forever.
- Every path has a boundary statement: the input ranges and configurations
  inside which fp16 gives acceptable results, derived from analysis of the
  arithmetic, confirmed by a test scene that sweeps across the boundary.
- Paths that feed each other are tested as chains, not only alone.

## Facts established (2026-10-29)

- **No shading code used fp16 before this plan** (mask 0 still does not). `erhe_half.glsl` defines `half`,
  `half_vec2..4` and `half_mat*` as `float16_t` types when the device
  reports `shaderFloat16` (`ERHE_HAS_SHADER_FLOAT16`, with the extension
  directive `GL_EXT_shader_explicit_arithmetic_types_float16` emitted by
  `shader_stages_create_info.cpp`) and as `float` otherwise; nothing in
  `standard.frag`, `erhe_bxdf.glsl` or `erhe_light.glsl` uses them (the
  word "half" there is the half vector). The Mali report shows 0% 16-bit
  arithmetic in every `standard` variant; the only fp16 shaders are the
  occlusion test shaders (100%). 16-bit storage access is gated the same
  way (`ERHE_HAS_STORAGE_BUFFER_16BIT_ACCESS`, `..._UNIFORM_AND_...`,
  `..._PUSH_CONSTANT_16`).
- **The render target is fp16.** The scene color target holds fp16 (a
  relative step of 2^-11; `render_scene_image` `linear` output documents it,
  `doc/editor/rendergraph.md`), and the output stage writes
  `to_render_target_range(color * exposure, c_output_max)` with
  `c_output_max = m_half_max`. Any error below one fp16 ulp of the written
  value is invisible by construction; that is the acceptance scale.
- **Where the registers go** (Mali-G310, static, `doc/erhe/graphics.md`
  "Mali analytics"): every lit `standard` fragment variant uses 64 work
  registers at 50% occupancy; with 4 shadowed directional lights at 24-bit
  depth they also spill (34 B isotropic, 82 B anisotropic) and are load/store
  bound; with 1 light at 32-bit depth they spill nothing and are arithmetic
  bound at 15.3 (isotropic) and 19.2 (anisotropic) cycles. The unlit
  variant uses 22 registers at full occupancy and is load/store bound on
  record fetches.
- **Upper bound experiment**: converting every float op of the recorded
  optimized modules to fp16 with spirv-opt (`--relax-float-ops
  --convert-relaxed-to-half`; numerically unacceptable for positions and
  shadow math, so a bound, not a design) and compiling on Mali-G310:
  isotropic 1 light goes from 64 registers / 50% to 32 / 100% at the same
  cycle count; the 4-light variants lose their spills (load/store 19.8 to
  15.0 and 24.0 to 15.0) but stay at 64 registers; the anisotropic 1-light
  variant stays at 64; the unlit variant is unchanged. The fp16 share was
  67 - 73%. So fp16 removes spills everywhere and doubles occupancy where
  the pressure is moderate (the low-end case), and the 4-light and
  anisotropic paths need their live ranges shortened as well.
- **Vulkan fp16 arithmetic needs `shaderFloat16`** (Mali and Adreno report
  it; Intel here does). Denormal handling differs per driver:
  `VK_KHR_shader_float_controls` reports whether fp16 denormals flush to
  zero or are preserved; erhe queries it (section 5).
- **Verification tools that exist**: `render_scene_image` with
  `output: "linear"` and `color_format: "rgba32f"` captures the scene color
  before post-processing as fp32 `.pfm` (`doc/editor/rendergraph.md`); the
  shadow stations and their analytic ground truth
  (`scripts/creations/creation_25_shadow_test_rooms.py`,
  `scripts/shadow_verify.py`, gates G1 - G7 in `doc/erhe/shadows.md`); the
  GI rooms and `scripts/gi_verify.py`; the Mali analytics pass for registers,
  spills and the fp16 share per variant; NVIDIA FLIP in the graphics GPU
  tests (`doc/erhe/graphics_test_coverage.md`).

## Design

### 1. The switch: `ERHE_FP16_PATHS`

One integer `Shader_key` axis, `ERHE_FP16_PATHS`, a bit mask with one bit
per path of section 3. `erhe_standard_variant.glsl` derives a macro per
bit (`ERHE_FP16_MATERIAL`, `ERHE_FP16_BRDF`, ...), each only when
`ERHE_HAS_SHADER_FLOAT16` is defined, so a device without fp16 arithmetic
compiles the fp32 chain whatever the mask says (and `half` is `float` there
anyway). The mask is a scene-level axis seeded like the light counts: the
graphics preset field `shader_fp16_paths` (preset version 6, default 0
until the gates pass, then the low-end set), set by the Settings window
checkboxes and `set_graphics_preset shader_fp16_paths`; the variant cache compiles the combinations a
session uses. The axis is compact-coded `H` in the variant browser.

The GLSL of each path is written once with `half` types and the path
macro, never duplicated: a function such as `isotropic_brdf()` takes and
returns `half` types under `ERHE_FP16_BRDF`, `float` otherwise, through a
small set of path-local type macros (`brdf_float`, `brdf_vec3`, ...) defined in
`erhe_fp16_paths.glsl` (included by `erhe_standard_variant.glsl`) from the
path bits. A value that crosses from
one path to another is converted at the boundary explicitly, so the chain
structure is visible in the source.

### 2. Acceptance scale and gates

Every path, alone and in its chains, is gated against the fp32 chain on the
same scene and camera with `render_scene_image` `linear` `rgba32f` at
`msaa_samples: 0`:

- **G-display**: the linear image with the exposure applied, clamped to
  [0, 1] and sRGB-encoded to 8 bits (what a display shows): for 99.9% of
  pixels every channel is within 4/255 of the reference, and no channel is
  off by more than 16/255. The script also reports the fp16 ulp statistics
  (relative error where the reference is at least 2^-4, absolute error
  below) without gating them: each fp16 operation rounds by up to 2^-12,
  so an ulp-scale gate rejects any fp16 chain of more than a few
  operations (the first run used two ulps and failed every BRDF row at
  3 - 10 ulps with no visible difference).
- **G-finite**: no NaN or infinity in any pixel.
- **G-dark**: no pixel that the fp32 image lights above 2^-10 goes to zero
  (fp16 underflow or flush-to-zero).
- **G-shadow**: with the shadow paths untouched by design, the shadow
  stations' G1 - G5 results are byte-identical between fp32 and the mask
  under test (`scripts/shadow_verify.py --set shader_fp16_paths=<mask>`), because no
  fp16 path may change what feeds the depth compare.
- **G-cost**: the Mali analytics of the recorded variants show, per path,
  the register count not higher and the fp16 share higher than with the
  path off; the plan's purpose is registers and spills, so a path that
  gains neither is dropped unless the user keeps it (P4).

`scripts/shader_fp16_verify.py` runs the matrix: loads each station of the
test scene (section 4), renders the fp32 reference once per view, then
every mask of the matrix, writes the error statistics and the difference
images to `logs/shader_fp16/<timestamp>/`, prints a PASS / FAIL table and
exits non-zero with `--enforce`. The matrix is: each path alone; each
chain prefix in the order of section 3; the full low-end set; and, for the
interaction cases named in section 3, the named pairs.

### 3. The paths, in build order

Each path states what it converts, the analysis that bounds it, the test
that checks the bound, and what it interacts with. The low-end set is
P1 - P5; P6 - P8 extend to the desktop configurations.

**P1 Material inputs** (`ERHE_FP16_MATERIAL`). The sampled and
record-sourced material values after the texture fetches: base color (and
vertex color), metallic, roughness, occlusion, emissive, alpha. Analysis:
8-bit and sRGB texture values are exact in fp16; vertex colors are unorm;
roughness is clamped to `c_roughness_floor` and its square and fourth power
are used by the BRDF, so the floor must keep `r^4` a normal fp16 number:
`r >= 0.09` for `r^4 >= 6.1e-5`. The current floor is 1e-4 (1e-7 for the
engine-ready anisotropic model, `standard.frag`), far below that, so P1
raises it to 0.09 on the fp16 path only: the fp32 path keeps its floors,
and station A shows what the fp16 path loses between 0.02 and 0.09
(sharper-than-fp16 highlights, which the 2^-11 `NdotH` resolution of P2
cannot render anyway). Emissive is HDR and stays fp32 unless below 2^15. Test:
station A (roughness and metallic sweep) and station C (HDR emissive
ramp). Interacts with P2 and P3 (feeds both).

**P2 Isotropic BRDF** (`ERHE_FP16_BRDF`). `isotropic_brdf()`: N, V, L, H
and their dot products, the GGX distribution, the visibility term, the
Schlick Fresnel, the diffuse term. Analysis: the failure is the
distribution `D = a2 / (pi (NdotH^2 (a2 - 1) + 1)^2)` near `NdotH = 1`,
where fp16 resolves `NdotH` in steps of 2^-11 and the highlight of a
low-roughness material collapses into a few bands; the fix that keeps the
path in fp16 computes `1 - NdotH^2` as `|cross(N, H)|^2` (the vector is
small near the peak and keeps relative precision) with the denominator
normalized by alpha^2, so the bound is the peak 1 / (pi alpha^2) <= 65504,
roughness >= 0.064 (P1's floor 0.09 keeps a margin). Fresnel and the diffuse
term are products of values in [0, 1] and are exact to one ulp. The vectors
N, V, L, H stay fp32 and only their dot products are fp16 (fp16 unit
vectors measured 13 - 76% errors at sharp flanks). Test: station A at every roughness and metallic
with the light at 15, 45 and 85 degrees; station B (grazing ramp,
`NdotL` from 1 down to 2^-10). Interacts with P1 (its inputs), P3 (its
output), P6 (the normal it receives).

**P3 Light accumulation** (`ERHE_FP16_ACCUMULATE`). `color +=
intensity * visibility * brdf` across the light loops and the ambient term
`ambient * base_color * occlusion + emissive`. Analysis: fp16 overflows at
65504; radiance times BRDF times the number of lights must stay below it
before the exposure multiply, and the dark end underflows below 6.1e-5
(normal) or 6e-8 (denormal, flushed on some drivers). The bound is stated
as: per-light `radiance * brdf <= 2^14` and the sum of the lit terms
`>= 2^-13` where the image shows them, which holds for radiance up to about
1000 with one light. Where exposure is small (very bright scenes), the
exposure multiply moves into the accumulation (`color` accumulates
`exposure * ...`), which keeps the sum in range at both ends; the fp32 path
keeps its order (exposure multiplies the finished sum). Test: station C (radiance
ramp from 2^-12 to 2^14 at three exposures) and station D (dark room:
ambient 2^-10 on base color 2^-6). Interacts with P1 and P2 (inputs), P5
(the visibility factor), and with the denormal behaviour of the driver,
which the device logs (section 5).

**P4 Attenuation and spot terms** (`ERHE_FP16_ATTENUATION`).
`get_range_attenuation()` and `get_spot_attenuation()`. Analysis: the
range term divides a squared distance by a squared range; the squares
overflow fp16 beyond 255 m, so the fp32 ratio `distance / range` is formed
first and only the polynomial of that ratio in [0, 1] runs in fp16; the
spot term is a smoothstep of cosines in [0, 1], exact to one ulp. Test:
station E (point and spot lights over 0.1 m to 500 m, spot cones from 1 to
89 degrees). Interacts with P3 (feeds the intensity).

**P5 Visibility factor** (`ERHE_FP16_VISIBILITY`). The *result* of
`sample_light_visibility()` as `half` once it is a factor in [0, 1]; the
shadow lookup, the receiver plane, the derived bias and the depth compare
stay fp32 (D1 - D12 of `doc/erhe/shadows.md` depend on them). Analysis: the
factor is a count of passed taps over the kernel size, exact in fp16 for
every kernel the presets use. Test: G-shadow on `contact_blocks`,
`cornell` and `thin_walls` with one directional light, hard filter, depth
technique (the low-end configuration), then the full core matrix.
Interacts with P3 only.

**P6 Normal and tangent frame** (`ERHE_FP16_FRAME`). The normalized
interpolated normal, the normal-map decode and the TBN combination, after
the fp32 interpolants are read. Analysis: a unit vector in fp16 has a
component error of 2^-11, an angular error of 0.03 degrees; `normalize()`
of an fp16 vector whose length is far from 1 loses precision, so the fp32
interpolant is normalized before conversion; the specular anti-aliasing
kernel takes derivatives of the normal (`dFdx`, `dFdy`), which in fp16 lose
the small differences the kernel is built from, so the SAA path reads the
fp32 normal and is outside this bit. Test: station F (normal-mapped tiles
with the finest-detail normal map, flat shading check: the fp16 frame at
every pixel within 0.1 degree of fp32 by a debug mode that outputs the
normal). Interacts with P2 (feeds it) and excludes SAA.

**P7 Anisotropic BRDF and brushed metal** (`ERHE_FP16_BRDF` extended).
Desktop configurations; the same bound as P2 per axis of roughness, with
the brushed-metal texgen in fp32. After P1 - P6 pass and the 4-light live
range work (section 5) is understood.

**P8 Vertex stage** (`ERHE_FP16_VERTEX`). Skinning of normals and tangents
(positions stay fp32), joint weights as fp16, and fp16 varyings
(`StorageInputOutput16`) for colors, texcoords and the TBN where the
device supports them. Analysis: the varying unit cost and the Position
shader's register count; texcoords in fp16 lose precision beyond 2048
texels, so fp16 texcoord varyings are gated on the texture size the
material binds (a per-draw fact, so this bit is decided per material, not
per scene). Test: station F at 4096-texel textures and the skinned test
asset (`creation_22`). Last, because the measured vertex cost is skinning
in the Position shader, which stays fp32.

### 4. Test scene: `creation_26_fp16_test_rooms.py`

A creation in the shape of the shadow stations: each station its own
scene with one directional light (the low-end configuration), scene
ambient settable, materials from the scene, and the views and sweeps as
Python data the verify script imports. Stations:

- **A Roughness and metallic sweep**: a 10 x 2 grid of spheres, roughness
  0.02 to 1.0 (log-spaced, crossing the floor), metallic 0 and 1, base
  color mid-gray and a saturated red; light at 15, 45 and 85 degrees from
  the view direction. Exercises P1, P2, the roughness floor and the
  highlight peak.
- **B Grazing ramp**: one large sphere lit from the side (`NdotL` sweeps 1
  to 0 across the terminator) and from behind the view (grazing view
  angles). Exercises P2 and P6 at the small-dot-product end.
- **C HDR radiance ramp**: a white floor under one directional light whose
  radiance steps 2^-12 to 2^14, rendered at exposures 2^-8, 1 and 2^4;
  emissive strips 2^-12 to 2^10 beside it. Exercises P1 (emissive), P3
  overflow and underflow, and the exposure ordering.
- **D Dark room**: ambient 2^-10, base colors 2^-6 to 2^-2, one weak light;
  the image is nearly black and every pixel is near the fp16 denormal
  range. Exercises P3 and the driver's flush-to-zero behaviour (G-dark).
- **E Attenuation range**: point and spot lights at distances 0.1 m to
  500 m from receivers, spot cones 1 to 89 degrees. Exercises P4.
- **F Normal-mapped tiles**: tiles with a high-frequency normal map, a
  vertex-colored tile, a 4096-texel texture tile; a debug output mode
  renders the shading normal. Exercises P6 and P8.
- **Shadow stations**: `creation_25` `contact_blocks`, `cornell`,
  `thin_walls` reused as they are, hard filter, depth technique.

The reference values are not analytic; the fp32 render is the reference and
the gates of section 2 measure the distance to it.

### 5. Device facts and the 4-light live range

- Device init queries `VK_KHR_shader_float_controls` (core 1.2) and logs
  `shaderDenormFlushToZeroFloat16`, `shaderDenormPreserveFloat16` and the
  rounding modes, and publishes them in `Device_info` so G-dark's expected
  behaviour is known per device; the shader sets nothing (the defaults are
  the driver's) until a test shows a need.
- The 4-light and anisotropic variants stay at 64 registers even in the
  all-half experiment, so their pressure is live state across the light
  loop and the anisotropic frame. Before P7, the variant with 4 shadowed
  lights is recorded with the light loop's per-light shadow terms scoped to
  the iteration (no value from the shadow lookup outlives its light) and
  measured again; this is fp32 work and its own small step, gated by
  G-shadow and the image gates.

## Build order and state

Done 2026-10-29: step 0 (axis,
preset field, type macros, float controls query, stations A - E, verify
script, unit tests) and the GLSL of P1 - P5 with their gates measured
under G-display (522 of 546 rows; the failures are the measured
flush-to-zero boundaries above). Not done: station F, the Default step (Next, item 4),
P6 - P8, and the register target, which the paths do not reach.

Next, in order:

1. **R1 Two-phase fragment**: compute every light's shadow visibility
   (`sample_light_visibility()` / the point cube lookup) in a first phase
   right after the receiver plane, before the material fetches and the
   BRDF, into a per-light float array sized by the key's light count on
   the loop path (the clustered path keeps its single loop); the light
   macros read the array. The N.L > 0 test moves to the shading phase (a
   visibility computed for a back-facing light is discarded there).
   Gates: `scripts/shadow_verify.py` byte-identical images (the visibility value
   does not change), `scripts/shader_fp16_verify.py` self rows zero, the
   desktop gate scripts. Measured alone it stays at 64 registers; it is
   half of R2's result.
2. **R2 Per-light orthographic constants**: `Light_shadowed` (light record
   hot half, `light_buffer.cpp`, `erhe_light_record.glsl`) gains four
   vec4 for directional lights: D_u, D_v, D (with |D|) and the projection
   row magnitudes of `texture_from_view_relative`; `sample_light_visibility()`
   reads them when the light is directional instead of forming them from
   `view_relative_from_texture` and the transposed plane product. Gate:
   `shadow_verify.py --matrix core` identical to the baseline run (the
   values are the same numbers computed once per light). With R1 the
   directional one-light hard-filter variant measures 32 registers at
   100% occupancy, 9.9 cycles (was 64 / 50% / 11.4).
3. **R3 Perspective lights**: spot and point lookups are 29 registers over
   the budget even without the plane and the minimum bias; reformulate
   their bias terms with fewer vector temporaries (the three D vectors
   and their lengths, the lateral tap reach) and measure each change with
   the offline recipe below. Separate investigation; the low-end
   configuration does not need it.
4. **Default**: `shader_fp16_paths`
   defaults to the low-end set on devices with `shaderFloat16` once the
   user decides the open items; the desktop image gates rerun with it.
5. **Station F, P6, P7, P8** as planned above, each with its station and
   chain gates.

### Offline register measurement recipe

The register budget rows were measured without the editor: take the
fragment preamble of a recorded variant (the "Sources" tab of the
browser page, the synthesized `erhe_preamble.glsl` block of the fragment
shader; it holds every define of the key), add
`#extension GL_GOOGLE_include_directive : enable` after its `#version`
line, append `res/shaders/standard.frag`, compile with
`glslang -V --target-env vulkan1.2 -S frag -I<shader dir>`, run
`spirv-opt -O`, then `malioc --vulkan --spirv -c Mali-G310 --format json`
and read `properties` `work_registers_used` / `thread_occupancy` and
`performance.total_cycles`. The baseline reproduces the editor's 64
registers (its cycle count differs from the browser's because erhe's
optimizer pass list differs from `-O`). A copy of `res/shaders` with one
edit per experiment, or an edited preamble (light counts, filter, bias
mode, fp16 mask), is one measurement; each takes about a second. The
Vulkan SDK provides glslang and spirv-opt; malioc is per machine
(`memory-bank/local/context.md`).

Recording the editor's own variants for `scripts/shader_analytics.py`
needs the SPIR-V cache set aside (`mv spirv_cache spirv_cache_bak`
before the run, restored after): cached modules are not recorded.

## Open

- User decisions pending: whether P1 keeps roughness in fp32 (its peak
  rounding is 1/255 under G-display); whether isolated black silhouette
  pixels on roughness 0.09 metals under P2 are acceptable or sharp metals
  get a higher floor; R3's priority.
- The driver here (Intel, Mesa) flushes fp16 denormals to zero by default
  although the float controls report both behaviours available; the
  capabilities say what can be set, not the default, so G-dark is the
  only reliable source of a device's behaviour.
- Whether Adreno (Quest) flushes fp16 denormals; G-dark's expectation on
  that device is read from the float controls log at the first run.
- Whether `half` varyings (P8) need `storageInputOutput16` on every target
  or stay fp32 interpolants converted in the fragment shader; the Mali
  varying pipeline cost decides, measured in the analytics.
- Materials with roughness below 0.09 render differently on the fp16 path
  (P1's floor); whether any shipped scene relies on sharper highlights is
  checked by station A and the default scenes in the Default step before the low-end
  set becomes the default.
