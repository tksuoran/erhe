# Leaf material: double-sided reflection and diffuse transmission

Status: proposed

Extends [../erhe/scene_renderer.md](../erhe/scene_renderer.md) (the forward
renderer and its uber-shader) and
[../erhe/shader_variants.md](../erhe/shader_variants.md) (the `ERHE_BXDF_MODEL`
variant axis). The material properties live in `erhe::primitive::Material`
([../erhe/primitive.md](../erhe/primitive.md)).

A new BxDF model, `Bxdf_model::leaf`, for thin vegetation surfaces such as
leaves, grass blades and petals. The first version is texture-free: every
input is a material constant. Its requirements:

1. The material is double sided: both faces are drawn and both cast shadows.
2. Each face has its own reflective parameters (a leaf's top (adaxial) side
   is usually glossier and darker than its bottom (abaxial) side).
3. Light that hits one face reaches the viewer on the other face as diffuse
   transmission, and light on the viewer's side is reflected.

## Sources

Two references guide the model. Only the parts named here are used.

- R. Habel, "Real-time Rendering and Animation of Vegetation", PhD thesis,
  TU Wien, 2009, chapter 3 "Leaf Rendering" (sections 3.3.1 - 3.3.5):
  - A leaf is a thin slab with rough front and back surfaces. Direct light
    splits into `L = Lr + Lt`; only one of the two is non-zero per light,
    chosen by the sign of `N . L` (section 3.3.1).
  - Reflection is Cook-Torrance on the front side and diffuse on the back
    side of typical broad leaves; this is a property of the leaf, not of the
    model, so both sides take a full specular parameter set
    (section 3.3.3). Measured ranges (Bousquet et al.): effective refractive
    index `n = 1.2 - 1.7`, Beckmann roughness `sigma = 0.078 - 0.5`, from
    highly specular (laurel) to nearly diffuse lobes (hazel).
  - Subsurface scattering hardly changes the light-facing side; it is what
    the opposite side shows (section 3.3.4). Multiple scattering inside the
    leaf destroys the direction of incident light, so the transmission is
    very diffuse (section 3.3). The light transmitted through either side
    is nearly the same; the side difference is in the reflection
    (section 3.2.3).
  - The surface transmittance at each face is `rho_t = 1 - rho_d`, the light
    that the face does not reflect (equation 3.9).
  - The thesis's multi-dipole BSSRDF, convolved over thickness, albedo and
    normal maps and baked into the Half-Life 2 basis (sections 3.3.5 -
    3.3.9), needs per-leaf textures. With no textures, a spatially uniform
    leaf integrates that kernel to a constant, so it becomes the diffuse
    BTDF used here. The baked form is future work ("Later").
- M. A. Greiner, B. D. Duncan, M. P. Dierking, "Bidirectional scattering
  distribution functions of maple and cottonwood leaves", Applied Optics
  46(25), 6485 - 6494, 2007 (sections 4 - 7). The measurements are at one
  near-IR wavelength, so the absolute values do not carry over to visible
  light. The shape of the data does:
  - The BTDF is Lambertian at every illumination angle measured (0 - 78
    degrees), which supports a constant BTDF.
  - The BRDF is Lambertian below about 50 degrees of incidence. Above that a
    forward specular peak grows and carries all reflection at grazing
    incidence, which is Fresnel-weighted microfacet behavior.
  - Transmission falls and reflection rises as the incidence angle grows
    (Tables 1 - 2), which is the `(1 - F)` weighting below.

## Current state the plan builds on

- `Material::double_sided` (and `Gprim::double_sided`, combined by
  `erhe::scene::is_double_sided()`) selects only the forward pipeline's cull
  state (`Forward_renderer`, `bucket.double_sided`). It is not a shader
  input.
- `standard.frag` never reads `gl_FrontFacing`: a back face shades with the
  front face's normal, so a double-sided plane lit from the front shows the
  same lit shading from behind.
- The shadow pass picks its pipeline by the preset's `Shadow_cull_mode` and
  `negative_determinant` only (`shadow_renderer.cpp`). Under the default
  `cull_back`, a double-sided single-layer surface whose front face points
  away from the light casts no shadow.
- The light loop macros in `standard.frag`
  (`ERHE_SHADE_DIRECTIONAL_SHADOWMAPPED` and the five others) skip a light
  when `dot(N, L) <= 0`.
- Shadow visibility is sampled per shadow record before the light loop. The
  normal-offset path offsets along `shadow_normal = N`, the interpolated
  normal.
- `Material::transmission` is glTF `KHR_materials_transmission` (specular,
  refractive transmission through glass). It stays as it is; diffuse
  transmission is a separate input.
- fastgltf parses and exports `KHR_materials_diffuse_transmission`
  (`fastgltf::MaterialDiffuseTransmission`); `gltf_fastgltf.cpp` does not
  read it yet.
- The ray query shader (`erhe_ray_hit.glsl`, `evaluate_bxdf()`) dispatches on
  `material.bxdf_model` at run time and flips `shading_normal` toward the
  ray.

## Model

The parameters, all constants:

| Property | Type | Default | Meaning |
|---|---|---|---|
| `base_color` (existing) | vec3 | 1, 1, 1 | Front face diffuse albedo |
| `roughness` (existing, `.x`) | float | 0.5 | Front face roughness |
| `reflectance` (existing) | float | 0.5 | Front face specular F0 (Filament mapping `F0 = 0.16 r^2`) |
| `back_base_color` (new) | vec3 | 1, 1, 1 | Back face diffuse albedo |
| `back_roughness` (new) | float | 0.5 | Back face roughness |
| `back_reflectance` (new) | float | 0.5 | Back face specular F0 |
| `diffuse_transmission` (new) | float | 0.0 | Fraction of the diffuse lobe that is transmitted |
| `diffuse_transmission_color` (new) | vec3 | 1, 1, 1 | Tint of the transmitted light |

`metallic` does not apply to the leaf model and is read as 0.

The diffuse terms follow `KHR_materials_diffuse_transmission`, so a glTF
round trip keeps the meaning and the BSDF conserves energy by construction:

```
side          = gl_FrontFacing ? front : back
N_s           = gl_FrontFacing ? N : -N                 (shading normal toward the viewer)
albedo, alpha, F0 = the parameters of `side`
t             = diffuse_transmission
F             = fresnel_schlick(dot(V, H), F0)          (as in isotropic_brdf)

dot(N_s, L) > 0 (light on the viewer's side):
    f = D * Vis * F  +  (1 - F) * (1 - t) * albedo / pi
    contribution = f * dot(N_s, L)

dot(N_s, L) < 0 (light on the far side):
    f = (1 - F_v) * t * diffuse_transmission_color / pi,
        F_v = fresnel_schlick(dot(N_s, V), F0)
    contribution = f * (-dot(N_s, L))
```

- The reflective branch is `isotropic_brdf()` with its diffuse term scaled by
  `(1 - t)`. This is Habel's Cook-Torrance front side, with the same lobe
  available on the back side.
- The transmissive branch is Lambertian in both directions (Greiner), and its
  color is shared by both faces (Habel 3.2.3). The exit Fresnel `(1 - F_v)`
  makes the transmission fall off toward grazing view, the trend in
  Greiner's Tables 1 - 2.
- Ambient term: a uniform ambient light reaches both hemispheres, so it
  shades with `albedo * (1 - t) + t * diffuse_transmission_color`. With DDGI
  it is `E(N_s) * albedo * (1 - t) + E(-N_s) * t * diffuse_transmission_color`
  (two irradiance reads).

Suggested presets from the measured ranges: `n = 1.2 - 1.7` gives
`F0 = ((n - 1) / (n + 1))^2 = 0.008 - 0.067`, that is `reflectance = 0.23 -
0.65`. Beckmann `sigma = 0.078 - 0.5` taken as GGX `alpha` gives
`roughness = sqrt(alpha) = 0.28 - 0.71`.

## Steps

Each step is one commit and is verified by its own check (`AGENTS.md`
"When to run the slow test suites"). Step 1 changes how every existing
double-sided material renders; it follows the glTF `doubleSided` rule (the
back face's normal is reversed), so it lands first and on its own.

### 1. Back faces of double-sided materials

- `standard.frag`: when the fragment is a back face, negate `N`, `T` and `B`
  before the normal map is applied. `gl_FrontFacing` accounts for the
  winding flip of mirrored (`negative_determinant`) buckets, because those
  pipelines swap their front face; the check below confirms it.
- Backends: confirm that `gl_FrontFacing` reaches the Vulkan, OpenGL and
  Metal shaders. The Metal shaders come from SPIR-V through SPIRV-Cross,
  which maps it to `[[front_facing]]`.
- Check: a headless render of a double-sided plane lit from the front, seen
  from both sides through `render_scene_image`. The back side reads dark
  (ambient only). Also a mirrored (negative scale) double-sided plane.

### 2. Double-sided casters in the shadow pass

- `Shadow_renderer` takes `bucket.double_sided` into its pipeline choice:
  a double-sided bucket uses the `cull_none` pipeline of the active
  depth-clamp family, whatever the preset's `Shadow_cull_mode` is.
- Check: a double-sided plane whose front face points away from a
  directional light shadows the ground under it.
- Document the rule in `doc/erhe/shadows.md` "Cull mode".

### 3. Material model and properties

- `Bxdf_model::leaf = 5` in `erhe_primitive/enums.hpp`, with `c_str()`,
  and `ERHE_BXDF_MODEL_LEAF 5` in `erhe_standard_variant.glsl`.
- The five new properties in `Material`, `Material_values` and the property
  registration (`material.cpp`), and the Properties window rows, which show
  only when `bxdf_model == leaf`.
- `Material_record_inputs` and the GPU material record gain the five values
  (`material_buffer.cpp`). The record layout change bumps the prepared asset
  version (no reading of old versions).
- The shader key derivation (`shader_key.cpp`): the leaf model needs the
  vertex normal like the other lit models; `test_shader_key.cpp` and
  `test_shader_define_constants.cpp` gain the leaf case.
- Selecting `leaf` on a material sets the material's `double_sided` to true.
- MCP: `mcp_server_material.cpp` accepts the model and the new fields.
- `CHANGELOG.md`: one line for the `erhe::primitive` API addition.
- Check: the touched unit test cases; an MCP round trip of the fields on a
  material.

### 4. Forward shading

- `erhe_bxdf.glsl`: `leaf_bsdf(...)`, the two branches of "Model", returning
  the already cosine-weighted contribution. It keeps the fp16 rules of
  `isotropic_brdf()` (`brdf_float`, the same denominator floors).
- `standard.frag`, leaf variant only: the face's parameters are selected once
  by `gl_FrontFacing`. In the six `ERHE_SHADE_*` macros the
  `N_dot_L > 0.0` gate becomes `N_dot_L != 0.0` (the BSDF picks the branch),
  through a macro so the other models compile as they do now.
- Shadow lookup for the transmissive branch: the fragment's visibility is
  the visibility of the light-facing face at the same point. In the leaf
  variant the normal-offset path offsets along the geometric normal turned
  toward the light (`sign(dot(N_g, L)) * N_g`), computed per shadow record
  in the visibility phase. The receiver-plane path is checked on a receiver
  facing away from the light. If its bias assumes a receiver facing the
  light, the leaf variant gets the same light-oriented receiver plane.
- Ambient and DDGI per "Model".
- Check (headless, `ERHE_DETERMINISTIC_CLOCK=1`, `ERHE_DEFAULT_LAYOUT=1`): a
  leaf plane with a green `diffuse_transmission_color`, `t = 0.5` and a
  directional light behind it. Seen from the unlit side it reads green and
  brighter than ambient; a second plane between the light and the leaf
  removes the transmitted light (shadowing works); with the light in front
  the two faces differ by their own parameters. Also the fp16 variant, and
  the Quest fragment shader variant when that plan's variant exists.

### 5. glTF import and export

- Import (`gltf_fastgltf.cpp`): a material with
  `KHR_materials_diffuse_transmission` and `diffuseTransmissionFactor > 0`
  becomes a `leaf` material. Its back side parameters copy the front side
  (glTF has one set), `diffuse_transmission` = the factor and
  `diffuse_transmission_color` = the color factor. The extension's textures
  are ignored, with a log line naming the material, until "Later" adds
  textures.
- Export: a `leaf` material writes the extension and `doubleSided: true`.
  The back side parameters, which glTF cannot express, travel in erhe's
  material property extension (`doc/gltf_extensions/`).
- Check: the Khronos sample asset `DiffuseTransmissionPlant` imports as leaf
  materials; an export / import round trip keeps every field.

### 6. Content library presets

- Materials "Leaf, glossy" (reflectance 0.65, roughness 0.3), "Leaf, matte"
  (reflectance 0.3, roughness 0.7), "Grass blade" and "Petal", with
  back-side albedo lighter than the front and `t` around 0.4. The foliage
  paint tool's nature kit plants use them
  ([foliage_paint_tool.md](foliage_paint_tool.md)).
- Check: the presets render in the content library thumbnails.

## Later

Out of scope for the texture-free version:

- Textures: per-side albedo and normal maps, a transmission color map and a
  thickness map; then Habel's baked translucency (multi-dipole kernel over
  the thickness and normal maps, stored in the Half-Life 2 basis) as a
  higher-quality transmission term.
- The ray query path (`erhe_ray_hit.glsl`): a `bxdf_model == 5u` case, with
  the face side taken from the hit's front-face flag. DDGI bounces and the
  lightmap baker then see leaves too. A lightmap texel holds one side's
  irradiance, so leaves keep the ambient/DDGI path until the baker stores
  both sides.
- USD: OpenPBR `geometry_thin_walled` with `subsurface_weight` /
  `transmission_weight` mapped to the leaf model.
