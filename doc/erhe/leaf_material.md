# Leaf material

Stability: experimental

`erhe::primitive::Bxdf_model::leaf` shades thin vegetation surfaces (leaves,
grass blades, petals): a double-sided surface with its own reflective
parameters per face and diffuse transmission of the light that reaches the
other face. Every input is a material constant; the model reads no texture of
its own. Remaining work: [../plans/leaf_material.md](../plans/leaf_material.md).

## Sources

- R. Habel, "Real-time Rendering and Animation of Vegetation", PhD thesis,
  TU Wien, 2009, chapter 3 "Leaf Rendering". A leaf is a thin slab with rough
  front and back surfaces; per light, only one of reflection and translucency
  is non-zero, chosen by the sign of `N . L` (3.3.1). Reflection is
  Cook-Torrance, with parameters that differ between the faces (3.3.3).
  Multiple scattering inside the leaf removes the direction of the incident
  light, so the transmission is diffuse (3.3), and the light transmitted
  through either face is nearly the same (3.2.3). The thesis's multi-dipole
  translucency over thickness and normal maps reduces, for a uniform leaf,
  to the constant diffuse transmission used here.
- M. A. Greiner, B. D. Duncan, M. P. Dierking, "Bidirectional scattering
  distribution functions of maple and cottonwood leaves", Applied Optics
  46(25), 6485 - 6494, 2007. Measured at one near-IR wavelength: the BTDF is
  Lambertian at every illumination angle, the BRDF grows a forward specular
  peak above about 50 degrees of incidence, and transmission falls as the
  incidence angle grows.

## Parameters

| Property | Meaning |
|---|---|
| `base_color`, `roughness` (`.x`), `reflectance` | The front face |
| `back_base_color`, `back_roughness`, `back_reflectance` | The back face |
| `diffuse_transmission` | Fraction of the diffuse lobe that is transmitted, 0 - 1 |
| `diffuse_transmission_color` | Tint of the transmitted light |
| `thickness` | Distance from the visible face to the far face, meters; 0 for a single-layer surface |

`metallic` does not apply. The Properties window shows the back face and
transmission rows only for a leaf material. The front face is the face whose
winding is front facing (`gl_FrontFacing`).

## Shading

`leaf_bsdf()` (`res/shaders/erhe_bxdf.glsl`) is the cosine-weighted BSDF; its
diffuse terms follow `KHR_materials_diffuse_transmission`, so the BSDF
conserves energy by construction. `N` is the shading normal turned toward the
viewer (`standard.frag` negates the frame on back faces), and the face the
viewer sees supplies albedo, roughness and reflectance:

```
t = diffuse_transmission
dot(N, L) > 0:  isotropic_brdf(albedo * (1 - t), roughness, 0, reflectance, L, V, N)
dot(N, L) < 0:  (-dot(N, L)) * (1 - F(dot(N, V))) * t * diffuse_transmission_color / pi
```

The transmissive lobe is Lambertian in both directions, and its color is the
same through either face. The exit Fresnel `F` uses the viewer face's
reflectance.

- Light loops: the leaf variant shades every light with `dot(N, L) != 0`
  (`ERHE_LIGHT_REACHES`); the other models keep `dot(N, L) > 0`.
- Ambient: the flat ambient term reaches both faces and shades with
  `albedo * (1 - t) + t * diffuse_transmission_color`. With DDGI, the
  viewer's side reads the irradiance along `N` and the transmitted side along
  `-N`. A lightmapped leaf uses the lightmap value for both sides.
- Shadows: transmitted light enters through the far face, so the shadow
  lookup of a light behind the surface is made `thickness` behind the
  fragment along `-N` (`ERHE_SHADOW_POSITION` in `standard.frag`). A closed
  leaf (a thin solid, as the level kit's leaves are) needs a thickness at
  least its own: at 0 its far face lies between the light and the lookup and
  shadows all of its transmitted light. A thickness larger than the leaf's
  moves the lookup into the air beyond it, toward the light. The
  receiver-plane lookups do not depend on the plane normal's sign; the
  normal-offset lookups offset along the geometric normal turned toward the
  light (`erhe_light.glsl`).

The ray query shader (`erhe_ray_hit.glsl`) shades a leaf material with the
isotropic BRDF.

## Double-sided drawing

A leaf material is drawn with its `double_sided` property set: glTF import
sets it from `doubleSided`, the content library's "Leaf" style sets it, and
the MCP `create_material` / `edit_material` tools set it when `bxdf_model`
becomes `leaf` without a `double_sided` field. Two rules hold for every
double-sided material, leaf or not:

- `standard.frag` shades a back face with the negated tangent frame (glTF
  `doubleSided`), which also mirrors the normal map.
- The shadow passes draw double-sided casters without face culling, so a
  single-layer surface casts a shadow whichever face points at the light
  (`doc/erhe/shadows.md` "Cull mode").

## glTF

- Import: a material with `KHR_materials_diffuse_transmission` and
  `diffuseTransmissionFactor > 0` becomes a leaf material; its back face
  copies the front (glTF has one set of reflective parameters), and
  `KHR_materials_volume` `thicknessFactor` gives its thickness. The
  extensions' textures are not read; a log line names the material.
- Export: a leaf material with `diffuse_transmission > 0` writes the
  extension, and `KHR_materials_volume` when its thickness is above 0; the
  back face rides `ERHE_material` (`back_base_color`,
  `back_roughness`, `back_reflectance`;
  [../gltf_extensions/ERHE_material.md](../gltf_extensions/ERHE_material.md)).

## Content library

`add_default_materials()` adds the "Leaf" style (`bxdf_model` leaf,
`double_sided`) and four materials on it: "Leaf, glossy", "Leaf, matte",
"Grass blade" and "Petal". Their reflectance (0.3 - 0.65) and roughness
(0.3 - 0.8) follow the measured leaf ranges cited by Habel 3.3.3: refractive
index 1.2 - 1.7 gives `F0 = ((n - 1) / (n + 1))^2 = 0.008 - 0.067`, reflectance
`sqrt(F0 / 0.16) = 0.23 - 0.65`; Beckmann roughness 0.078 - 0.5 taken as GGX
alpha gives roughness `sqrt(alpha) = 0.28 - 0.71`.

## Verification

`scripts/leaf_material_verify.py` (headless) imports a scene with a plain
double-sided quad, a `KHR_materials_diffuse_transmission` quad and a flipped
double-sided blocker quad and a closed 2 cm slab leaf, and checks the import,
the dark back face of the plain quad, the tinted transmitted light on the
leaf's back face, the blocker's shadow removing it, the slab transmitting as
much as the quad with its thickness and nothing at thickness 0, and
`diffuse_transmission` 0 removing the light. Unit
tests: `Material_leaf.*` (`erhe_primitive` tests), `Shader_key_leaf.*` and the
leaf cases of `Shader_key_*` (`erhe_scene_renderer` tests),
`Gltf_roundtrip.leaf_material_round_trips` (`erhe_gltf` tests).

## Future work

- [../plans/leaf_material.md](../plans/leaf_material.md): textures, the baked
  translucency of Habel's thesis, the ray query path, USD.
