# Material-aware ray traced rendering plan (issue #233, follow-up)

Builds on the primary-ray milestone (doc/raytrace-plan.md, commits 2cfbdadc +
ac0db2f5 on branch `raytrace`). Goal: the ray query compute shader shades hits
with real material data (base color, textures, smooth normals) and supports
transparent materials (glass) via refraction with an index of refraction, and
the material model round-trips through glTF.

Status: reviewed + implemented. Review decisions (2026-07-11): volume
attenuation stays deferred; glass gets a real traced reflection ray at each
interface (not the background-color approximation of the original D5); hits
sample the scene lights with ray traced shadows instead of N.V shading (the
original open question 3, answered "sample lights"). D1 was revised during
implementation - see the note inside D1.

## Current state (what exists today)

- `Ray_trace_renderer` (src/editor/renderers/ray_trace_renderer.{hpp,cpp}):
  BLAS cache per `Buffer_mesh`, per-frame TLAS (4 slots), compute dispatch
  with camera UBO + TLAS + rgba8 storage image. Shading is geometric-normal
  N.V only; the shader knows nothing about materials.
- `instance_custom_index` is written per TLAS instance but holds only the
  instance ordinal (ray_trace_renderer.cpp:364) - exactly the hook needed for
  a per-instance lookup table.
- The raster path already has everything material-related on the GPU:
  - `erhe::scene_renderer::Material_buffer` (material_buffer.cpp): per-frame
    ring-buffer upload of all materials into an SSBO (binding point 0, block
    `material`, array `materials`); assigns `material->material_buffer_index`
    sequentially during `update()`.
  - Texture heap: descriptor-indexed unsized `sampler2D erhe_texture_heap[]`
    at set 1 binding 0, auto-declared when the bind group layout sets
    `uses_texture_heap` (shader_stages_create_info.cpp:307-309). Material
    records store `uvec2` texture handles.
  - Mesh_memory pools are created with `Buffer_usage::storage`
    (mesh_memory.cpp:274), so index and vertex streams are already
    SSBO-readable. Stream 0 is position-only; stream 1 holds normal (vec3),
    tangent (vec4), tex_coord 0/1 (vec2 each), color (vec4) = 60 bytes
    per vertex, identical for skinned and non-skinned formats.
- BLAS geometry addressing (ray_trace_renderer.cpp:199-241): vertex range =
  `vertex_buffer_ranges[0]` (byte_offset + element_size stride), index range
  byte offset = `index_buffer_range.byte_offset + triangle_fill_indices.
  first_index * 4`; index values are relative to the range start (raster
  draws use base_vertex the same way).
- glTF: the pinned fastgltf fork already parses KHR_materials_ior /
  KHR_materials_transmission / KHR_materials_volume (extension bits are
  enabled in gltf_fastgltf.cpp:2282-2285), but `parse_material` ignores the
  parsed values and `process_material` leaves them default on export.
  `erhe::primitive::Material_data` has no ior/transmission fields.
- Editor scene save/load persists materials through the companion `data.glb`
  (scene_serialization.cpp:1090 -> `export_gltf`), NOT through scene.json.
  Anything not written by `process_material` does not survive save/load.

## Design decisions

### D1 (REVISED): per-instance buffer device addresses, not pool SSBO bindings

The original plan chose "bind the shared Mesh_memory pools as plain SSBOs and
store per-instance element offsets", on the assumption that all content
meshes live in exactly one index pool buffer and one stream-1 vertex pool
buffer. That assumption is false: `Mesh_memory` holds a *vector* of
`Buffer_pool`s (one per Vertex_stream instance, and separate pools for the
skinned / non-skinned / wireframe formats even when their stream layouts are
byte-identical - see the pool-selection comment in mesh_memory.cpp), and each
pool grows by allocating additional fixed-size buffer blocks
(`Buffer_pool_block_create_info::max_blocks`). Two instances' geometry can
therefore live in different `VkBuffer`s, which a fixed SSBO binding cannot
express.

Revised choice: **store per-instance buffer device addresses**
(`GL_EXT_buffer_reference` + `GL_EXT_buffer_reference_uvec2`) in the
instance-record SSBO. The addresses are pre-offset on the CPU side:
`index_address` points at the instance's first triangle index,
`vertex_address` at the start of its stream-1 vertex range.

Rationale:
- Handles multi-pool / multi-block geometry with zero descriptor plumbing.
- The infrastructure already exists precisely for acceleration structure
  builds: the pools carry `Buffer_usage::shader_device_address` when
  `use_ray_query` is on, and the device enables `bufferDeviceAddress`.
  Only a public `Buffer::get_device_address()` accessor was missing.
- The whole feature is gated on Vulkan ray query, so BDA's lack of a Metal
  analogue does not matter here (a Metal path would use `gpuAddress()`).

### D2: reuse `Material_buffer`, do not fork a ray-trace copy

`Ray_trace_renderer` already owns a `Camera_buffer`; it additionally gets its
own `erhe::scene_renderer::Material_buffer` (same `Material_interface` layout
as the raster path) and calls `update()` itself each enabled frame with the
materials referenced by the gathered instances. This reuses the existing GPU
material struct, texture-heap handle allocation, and the `Material` GLSL
struct (`material.materials[...]`) verbatim - the ray trace shader includes
the same interface. `material_buffer_index` is (re)assigned by whichever
update ran last, so the renderer must write its instance records *after* its
own `update()` call within the same frame (the raster passes do their own
updates independently; each consumer reads the indices it assigned).

### D3: traversal opacity stays opaque; transparency is shading-level

The bounce loop (D5) continues *new* rays from each hit instead of using
any-hit traversal, so `gl_RayFlagsOpaqueEXT` + BLAS `opaque = true` remain
correct and fastest for every trace: transmissive surfaces still want the
closest hit committed; what changes is what the shader does with it.
Non-opaque traversal (any-hit) is only needed for alpha-test cutouts, which
stay out of scope. Instance masks: keep 0xff for now but document a mask
policy - bit 0 (0x01) = all instances, bit 1 (0x02) = set only on
non-transmissive instances - so future shadow rays can trace with mask 0x02
to skip glass. Wiring the second bit is a one-line change when the instance
records exist (`material->data.transmission > 0` decides), so include it now.

### D4 (as implemented): instance record layout (std430 SSBO)

New GLSL block, ray-trace-only, filled per frame next to the TLAS instances
(declared via a `Shader_resource` block owned by `Ray_trace_renderer`, layout
verified against the CPU mirror struct at construction):

```glsl
struct Instance_record {
    uvec2 index_address;       // device address of the first triangle index
    uvec2 vertex_address;      // device address of the stream-1 range start
    uint  vertex_stride_uints; // stream-1 stride in uints (15 today)
    uint  material_index;      // index into material.materials[]
    uint  flags;               // bit 0 = transmissive
    uint  reserved0;
};
```

32 bytes per record. The addresses are dereferenced through a
`layout(buffer_reference)` `uint data[]` view; attribute fetch is manual
offset math + `uintBitsToFloat`. The per-attribute uint offsets within
stream 1 (normal, tex_coord0, color0) are derived from the Mesh_memory
vertex format at pipeline creation and passed as ERHE_RT_* defines, so the
shader stays in sync with mesh_memory.cpp.

Fetch at a hit:
```
prim  = rayQueryGetIntersectionPrimitiveIndexEXT
i0..2 = index_pool[rec.index_first + 3 * prim + k]           (k = 0,1,2)
attr  = stream1_pool[rec.vertex_base + i_k * rec.vertex_stride + field]
```
interpolated by `rayQueryGetIntersectionBarycentricsEXT`. Normals transform
to world space with the transpose of
`rayQueryGetIntersectionWorldToObjectEXT` (inverse-transpose of
object-to-world).

Upload: a dedicated `Ring_buffer_client` (mirroring Material_buffer usage) -
one allocation per enabled frame, records written in the same loop that
pushes `Acceleration_structure_instance`s, so `instance_custom_index` (still
the ordinal) is by construction the record index. `m_instances` and a
parallel record scratch vector keep high-water capacity (`clear()`, never
reassign) - no steady-state heap allocations. 24-bit custom index limits us
to 16M instances; fine.

### D5 (as implemented): glass = iterative bounce loop, deterministic, no accumulation

Implementation deltas from the sketch below (per user review): the reflected
branch at each transmissive interface is a REAL traced ray (one closest-hit
trace, shaded directly with the full light loop; its own transmissive hits
shade as opaque - no nested recursion), not a background-color
approximation. Opaque hits shade with the isotropic BRDF against the scene
lights (runtime counts from the light buffer, type-major order) with ray
traced shadow rays (mask 0x02 skips transmissive instances so glass does not
cast shadows), plus ambient and emissive - N.V shading is gone.

Ray queries cannot recurse; the shader runs a bounded loop (compile-time
`MAX_BOUNCES`, initially 8):

```
throughput = vec3(1); ray = camera primary ray;
for bounce in 0 .. MAX_BOUNCES:
    trace closest hit (opaque flags, mask 0x01)
    miss            -> color += throughput * background; break
    opaque hit      -> color += throughput * shade(material, N, V); break
    transmissive hit:
        entering = dot(N, ray.dir) < 0; N' = entering ? N : -N
        eta      = entering ? 1.0 / ior : ior
        F        = schlick(ior, cos_theta)          // fresnel
        refr     = refract(ray.dir, N', eta)
        if refr == 0 (TIR): ray.dir = reflect(ray.dir, N'); (F = 1 path)
        else:
            color      += throughput * F * background_approx(reflect dir)
            throughput *= (1 - F) * base_color.rgb   // tint
            ray.dir     = refr
        ray.origin = hit_pos + epsilon * ray.dir; continue
```

Deterministic single-path: the loop follows refraction (or reflection on
total internal reflection); the reflected branch at each interface is
approximated by the Fresnel-weighted miss/background color rather than a
second traced path. This is the honest non-stochastic compromise - true
Whitted branching needs a ray stack, and stochastic single-sample needs
accumulation, both explicitly out of scope. A hit counts as transmissive when
`material.transmission > 0`; throughput is additionally scaled by
`mix(1, base_color, transmission)` so partial transmission tints correctly.
Self-intersection is avoided by the epsilon offset along the new direction
(t_min 0.001 already exists).

### D6: material model additions (mirroring glTF extensions)

`erhe::primitive::Material_data` gains:

| field | type | default | glTF source |
|-------|------|---------|-------------|
| `ior` | float | 1.5f | KHR_materials_ior `ior` |
| `transmission` | float | 0.0f | KHR_materials_transmission `transmissionFactor` |

Volume parameters (KHR_materials_volume: thickness, attenuation color +
distance) are **deferred**: Beer-Lambert attenuation needs the traveled
in-medium distance, which the loop does provide (t between entry/exit hits),
but the import/UI/GPU surface triples for a second-order visual effect.
Listed as the natural follow-up; the plan leaves space in the GPU struct
ordering so appending them later is additive. `transmissionTexture` is also
deferred (factor only).

GPU `Material_struct` (material_buffer.cpp `Material_interface`): append
`ior` (float) and `transmission` (float) after `occlusion_texture_strength`.
Offsets are computed by `Shader_resource::add_float`, and the struct is
consumed via the generated GLSL declaration, so std140/std430 alignment is
handled by the existing machinery; two floats after an odd float tail keeps
packing tight (add one padding float only if the final struct size needs
rounding to 16 - verify against the generated layout during implementation).
The raster fragment shader ignores the new fields for now (a raster
approximation of transmission is out of scope).

Editing surface:
- Properties window (properties.cpp:1751-1757 block): add
  `add_entry("IOR", SliderFloat 1.0 .. 3.0)` and
  `add_entry("Transmission", SliderFloat 0.0 .. 1.0)`.
- MCP: `get_material_details` / `get_scene_materials` serialization
  (mcp_server_scene_query.cpp:444-466) and `edit_material` parsing
  (mcp_server_material.cpp:350-499) + tool schema
  (mcp_server_tool_list.cpp:236-259) gain `ior` and `transmission` keys;
  extend `mcp_server_tests.cpp` material round-trip assertions.

### D7: glTF import/export wiring

fastgltf (fork tksuoran/fastgltf, tag f0c81f93) already parses all three
extensions when the extension bits are set - and erhe sets them
(gltf_fastgltf.cpp:2282-2285). `fastgltf::Material::ior` is a plain scalar
member (default 1.5); `transmission` / `volume` are `std::unique_ptr`s that
are non-null when present.

- Import (`parse_material`, gltf_fastgltf.cpp:1280, in the PBRData block
  ~1358): `create_data.ior = material.ior;` and
  `if (material.transmission) create_data.transmission =
  material.transmission->transmissionFactor;`.
- Export (`process_material`, gltf_fastgltf.cpp:2860): set
  `gltf_material.ior = material->data.ior;` and, when
  `data.transmission > 0`,
  `gltf_material.transmission = std::make_unique<fastgltf::MaterialTransmission>(...)`.
  fastgltf emits the extension objects automatically when set.
- Because editor save/load rides `export_gltf` -> `data.glb` ->
  `parse_gltf`, this wiring **is** the (de)serialization asked for in the
  handoff - no scene.json change needed. The erhe extras carrier is not
  needed either: both values are expressible as standard extensions.
- `KHR_materials_volume` import/export: deferred with the volume fields (D6).

### D8: output stays in the developer window for this milestone

Recommendation: keep the "Ray Trace" developer window + `set_ray_trace` MCP
tool as the sole output surface for this plan. Composing into the viewport
via a `Texture_rendergraph_node` is real work that is orthogonal to
materials: it drags in viewport-sized (resizing) output textures, sRGB/tone
mapping consistency with post processing, and a policy for raster/RT mixing.
Do it as its own follow-up once material shading is worth looking at
full-screen. (The fixed 960x540 rgba8 target is kept until then.)

Accumulation and denoising remain explicitly out of scope.

## Implementation phases

### Phase A: materials + attributes at hit points

1. Instance record SSBO (D4): scratch vector + ring buffer upload in
   `Ray_trace_renderer::render()`; record fields from `Buffer_mesh` ranges
   and `mesh_primitive.material->material_buffer_index`.
2. Own `Material_buffer` (D2): gather referenced materials (scratch vector,
   high-water), `update()`, bind alongside camera/TLAS/output. Set
   `uses_texture_heap = true` on the bind group layout; add material block +
   instance block + index pool + stream-1 pool bindings.
3. Shader: fetch instance record via
   `rayQueryGetIntersectionInstanceCustomIndexEXT`, interpolate stream-1
   normal + tex_coord0 by barycentrics, read
   `material.materials[rec.material_index]`, sample base color texture via
   the heap with `textureLod(..., 0)` (no derivatives in compute; explicit
   LOD 0 now, ray cones later if ever needed), shade
   `base_color * n_dot_v` with the interpolated (smooth) normal.
4. Verify headless: textured + colored materials visibly match the raster
   viewport (screenshot + `set_ray_trace` PNG readback side by side).

### Phase B: material model + IO (independent of A, can land in parallel)

1. `Material_data::ior` / `transmission` + GPU `Material_struct` fields (D6).
2. Properties window rows, MCP get/edit/schema/tests (D6).
3. glTF import/export (D7); verify `export_gltf`/`import_gltf` round-trip
   and editor save/load keep the values (MCP: edit_material -> save_scene ->
   reload -> get_material_details).

### Phase C: refraction loop (needs A + B)

1. Bounce loop shader rewrite (D5); `MAX_BOUNCES` compile-time constant.
2. Instance mask bit for non-transmissive (D3) while the record loop is
   being touched.
3. Verify headless: glass sphere (transmission 1, ior 1.5) over a textured
   floor - expect inverted refracted image through the sphere, Fresnel
   brightening at grazing angles, TIR ring. Vary ior via `edit_material`
   between captures to see refraction change.

## Constraints honored

- No steady-state heap allocations: all per-frame containers are persistent
  scratch with `clear()` + high-water capacity; GPU uploads go through ring
  buffers (existing pattern in this renderer and Material_buffer).
- `class` not `struct` on the C++ side; explicit types; std430 blocks with
  explicit layout math (D4 record is exactly 16 bytes).
- Vulkan-only paths stay gated on `Device_info::use_ray_query`; SSBOs are
  guaranteed on that path (no UBO fallback needed for the new blocks).
- Headless verify loop (`erhe-headless-verify`) is the standard check after
  each phase.

## Open questions - resolved in review (2026-07-11)

1. Volume attenuation (Beer-Lambert) + transmissionTexture: deferred,
   confirmed.
2. Reflection: glass gets a real traced reflection ray at each interface
   (implemented in D5).
3. Shading: sample the scene lights (all types, runtime counts, traced
   shadows); N.V dropped (implemented in D5).
