# Material-aware ray traced rendering

Stability: mostly stable

The ray query compute shader shades hits with real material data (base color,
textures, smooth normals), samples the scene lights with ray traced shadows, and
refracts through transmissive materials with an index of refraction. The
material model round-trips through glTF. This builds on the GPU ray tracing
foundation in [`raytrace.md`](raytrace.md).

## Design decisions

### D1: per-instance buffer device addresses

Per-instance geometry is addressed by **buffer device addresses**
(`GL_EXT_buffer_reference` + `GL_EXT_buffer_reference_uvec2`) stored in the
instance-record SSBO, pre-offset on the CPU side: `index_address` points at the
instance's first triangle index, `vertex_address` at the start of its stream-1
vertex range.

Binding the shared `Mesh_memory` pools as plain SSBOs with per-instance element
offsets cannot express this. `Mesh_memory` holds a *vector* of `Buffer_pool`s
(one per `Vertex_stream` instance, and separate pools for the skinned,
non-skinned and wireframe formats even when their stream layouts are
byte-identical), and each pool grows by allocating additional fixed-size buffer
blocks (`Buffer_pool_block_create_info::max_blocks`), so two instances'
geometry can live in different `VkBuffer`s.

Device addresses need zero descriptor plumbing, and the infrastructure exists
for acceleration structure builds already: the pools carry
`Buffer_usage::shader_device_address` when `use_ray_query` is on, the device
enables `bufferDeviceAddress`, and `Buffer::get_device_address()` is public. The
whole feature is gated on Vulkan ray query, so the lack of a Metal analogue does
not matter here (a Metal path would use `gpuAddress()`).

### D2: reuse `Material_buffer`

`Ray_trace_renderer` owns an `erhe::scene_renderer::Material_buffer` with the
same `Material_interface` layout as the raster path, and calls `update()` itself
each enabled frame with the materials the gathered instances reference. This
reuses the GPU material struct, the texture-heap handle allocation and the
`Material` GLSL struct (`material.materials[...]`) verbatim.
`material_buffer_index` is (re)assigned by whichever update ran last, so the
renderer writes its instance records **after** its own `update()` call within
the same frame; each consumer reads the indices it assigned.

### D3: traversal stays opaque

The bounce loop (D5) continues *new* rays from each hit instead of using any-hit
traversal, so `gl_RayFlagsOpaqueEXT` and BLAS `opaque = true` stay correct and
fastest for every trace: a transmissive surface still wants the closest hit
committed, and what changes is what the shader does with it. Non-opaque
traversal would only be needed for alpha-test cutouts, which are out of scope.

Instance mask policy: bit 0 (0x01) is set on all instances, bit 1 (0x02) only on
non-transmissive ones (`material->data.transmission > 0` decides), so shadow
rays trace with mask 0x02 and glass casts no shadow.

### D4: instance record layout (std430 SSBO)

A ray-trace-only GLSL block, filled per frame next to the TLAS instances
(declared through a `Shader_resource` block owned by `Ray_trace_renderer`, with
the layout verified against the CPU mirror struct at construction):

```glsl
struct Instance_record {
    uvec2 index_address;       // device address of the first triangle index
    uvec2 vertex_address;      // device address of the stream-1 range start
    uint  vertex_stride_uints; // stream-1 stride in uints
    uint  material_index;      // index into material.materials[]
    uint  flags;               // bit 0 = transmissive
    uint  reserved0;
};
```

32 bytes per record. The addresses are dereferenced through a
`layout(buffer_reference)` `uint data[]` view, and attribute fetch is manual
offset math plus `uintBitsToFloat`. The per-attribute uint offsets within
stream 1 (normal, tex_coord0, color0) are derived from the `Mesh_memory` vertex
format at pipeline creation and passed as `ERHE_RT_*` defines, so the shader
stays in sync with `mesh_memory.cpp`.

Fetch at a hit:

```
prim  = rayQueryGetIntersectionPrimitiveIndexEXT
i0..2 = index_pool[rec.index_first + 3 * prim + k]           (k = 0,1,2)
attr  = stream1_pool[rec.vertex_base + i_k * rec.vertex_stride + field]
```

interpolated by `rayQueryGetIntersectionBarycentricsEXT`. Normals transform to
world space with the transpose of `rayQueryGetIntersectionWorldToObjectEXT`.

Upload goes through a dedicated `Ring_buffer_client` (mirroring `Material_buffer`
usage): one allocation per enabled frame, with records written in the same loop
that pushes `Acceleration_structure_instance`s, so `instance_custom_index` (the
ordinal) is by construction the record index. `m_instances` and the parallel
record scratch vector keep high-water capacity (`clear()`, never reassigned), so
steady-state frames perform no heap allocations. The 24-bit custom index limits
the renderer to 16M instances.

### D5: glass is an iterative bounce loop, deterministic, no accumulation

Ray queries cannot recurse, so the shader runs a bounded loop (compile-time
`MAX_BOUNCES`):

```
throughput = vec3(1); ray = camera primary ray;
for bounce in 0 .. MAX_BOUNCES:
    trace closest hit (opaque flags, mask 0x01)
    miss            -> color += throughput * background; break
    opaque hit      -> color += throughput * shade(material, N, V); break
    transmissive hit:
        entering = dot(N, ray.dir) < 0; N' = entering ? N : -N
        eta      = entering ? 1.0 / ior : ior
        F        = schlick(ior, cos_theta)
        refr     = refract(ray.dir, N', eta)
        if refr == 0 (total internal reflection):
            ray.dir = reflect(ray.dir, N')
        else:
            color      += throughput * F * traced_reflection(reflect dir)
            throughput *= (1 - F) * base_color.rgb
            ray.dir     = refr
        ray.origin = hit_pos + epsilon * ray.dir; continue
```

- An opaque hit shades with the isotropic BRDF against the scene lights (runtime
  counts from the light buffer, type-major order) with ray traced shadow rays,
  plus ambient and emissive.
- The reflected branch at each transmissive interface is a real traced ray: one
  closest-hit trace shaded directly with the full light loop. Its own
  transmissive hits shade as opaque, so there is no nested recursion.
- The refracted path is followed deterministically (or the reflected one on
  total internal reflection). True Whitted branching needs a ray stack and
  stochastic single-sample needs accumulation; both are out of scope.
- A hit counts as transmissive when `material.transmission > 0`; throughput is
  additionally scaled by `mix(1, base_color, transmission)` so partial
  transmission tints correctly. Self-intersection is avoided by the epsilon
  offset along the new direction, on top of the existing `t_min` of 0.001.

### D6: material model

`erhe::primitive::Material_data` carries:

| field | type | default | glTF source |
|-------|------|---------|-------------|
| `ior` | float | 1.5f | KHR_materials_ior `ior` |
| `transmission` | float | 0.0f | KHR_materials_transmission `transmissionFactor` |

The GPU `Material_struct` (`material_buffer.cpp` `Material_interface`) appends
`ior` and `transmission` after `occlusion_texture_strength`; offsets come from
`Shader_resource::add_float` and the struct is consumed through the generated
GLSL declaration, so std140 / std430 alignment is handled by the existing
machinery. The raster fragment shader ignores both fields.

Editing surface: Properties window rows ("IOR" 1.0 to 3.0, "Transmission" 0.0 to
1.0), the MCP `get_material_details` / `get_scene_materials` serialization,
`edit_material` parsing and the tool schema, plus the `mcp_server_tests`
material round-trip assertions.

### D7: glTF import and export

The pinned fastgltf fork parses KHR_materials_ior, KHR_materials_transmission
and KHR_materials_volume when the extension bits are set, and erhe sets them.
`fastgltf::Material::ior` is a plain scalar member (default 1.5); `transmission`
and `volume` are `std::unique_ptr`s that are non-null when present.

- Import (`parse_material`): `create_data.ior = material.ior;` and, when
  `material.transmission` is present, its `transmissionFactor`.
- Export (`process_material`): `gltf_material.ior` from `material->data.ior`,
  and a `fastgltf::MaterialTransmission` when `data.transmission > 0`. fastgltf
  emits the extension objects automatically when they are set.
- Editor save / load rides `export_gltf` -> `data.glb` -> `parse_gltf`, so this
  wiring **is** the serialization: nothing in `scene.json` and no erhe extras
  carrier are needed, because both values are standard extensions.

### D8: the output surface is the developer window

The "Ray Trace" developer window and the `set_ray_trace` MCP tool are the only
output surface. Composing into the viewport through a `Texture_rendergraph_node`
is orthogonal work: it drags in viewport-sized (resizing) output textures, sRGB
and tone mapping consistency with post processing, and a policy for mixing
raster and ray traced output.

## Constraints honored

- No steady-state heap allocations: every per-frame container is persistent
  scratch with `clear()` and high-water capacity, and GPU uploads go through
  ring buffers.
- `class` not `struct` on the C++ side, explicit types, std430 blocks with
  explicit layout math.
- Vulkan-only paths stay gated on `Device_info::use_ray_query`; SSBOs are
  guaranteed on that path, so the new blocks need no UBO fallback.
- The headless verify loop (`erhe-headless-verify`) is the standard check.

## Verification

- Textured and coloured materials visibly match the raster viewport: compare a
  screenshot with the `set_ray_trace` PNG readback side by side.
- A material round-trip keeps `ior` and `transmission`:
  `edit_material` -> `save_scene` -> reload -> `get_material_details`, and
  `export_gltf` / `import_gltf`.
- A glass sphere (transmission 1, ior 1.5) over a textured floor shows an
  inverted refracted image through the sphere, Fresnel brightening at grazing
  angles and a total-internal-reflection ring. Varying `ior` through
  `edit_material` between captures changes the refraction.

## Future work

- [plans/raytrace.md](../plans/raytrace.md) - volume attenuation, viewport
  composition, accumulation and denoising.
