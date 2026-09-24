# GPU and shader coding rules

Stability: stable

Rules for code that touches the graphics backends, shaders and shader
interface blocks. Backend designs: `doc/erhe/graphics.md`,
`doc/erhe/vulkan_backend.md`; the uber-shader variant system:
`doc/erhe/shader_variants.md`.

## Vulkan validation cleanliness

erhe is kept free of Vulkan validation errors at all times. When touching
Vulkan code, run the editor with the validation layer enabled
(`"vulkan_validation_layers": true` in `config/editor/erhe_graphics.json`)
and fix every VUID before committing - the editor's device-error callback
treats validation errors as fatal, so a single error aborts the run. Fix the
cause of every message; validation messages are never suppressed, filtered or
downgraded to make a run pass. Validation is off by default (per-frame cost),
so a change to a bind path, barrier or render pass needs an explicit
validation run.

## Shader interface block layout (UBO/SSBO)

When creating or modifying uniform blocks or shader storage blocks, respect
std140/std430 alignment explicitly:

- `vec4` and `vec3` fields start at an offset that is a multiple of 16 bytes.
- `vec2` fields start at an offset that is a multiple of 8 bytes.
- `float`, `int`, `uint` fields start at an offset that is a multiple of 4
  bytes.
- The struct's total size is a multiple of 16 bytes; add explicit padding
  fields to ensure this.
- After a `vec3`, add a `float` padding to reach the next 16-byte boundary.
- When a block has an odd number of `vec2`/`float` pairs, add a padding
  `vec2` or two `float` fields to round the struct size to a multiple of 16
  bytes.

Example: a block with `uvec2 texture_handle`, `vec2 uv_min`, `vec2 uv_max`
needs a `vec2 padding` appended to make the total size 32 bytes (2 x vec4).
