# Vulkan backend: known issues

Status: proposed

Extends `doc/vulkan_backend.md`.

## ID-buffer edge lines: line ends must be rounded in the fragment shader

The ID-buffer edge-line method (`content_edge_lines.use_id_buffer`) draws
wide-line quads into a standalone edge-id buffer (encoded face id in color,
plus the pass's own depth test), and the polygon-fill pass later matches that
buffer to paint the visible lines. The wide-line quads have **square end
caps**: the compute tent (`compute_before_content_line.comp`) extends each
half-quad past the edge endpoint along the edge axis, so the rasterized quad
covers a rectangular region at the line ends rather than a rounded (capsule)
one.

The edge-id fragment shader (`res/shaders/content_line_after_compute.frag`) is
responsible for trimming those caps to a rounded shape by `discard`ing
fragments beyond the cap. It carries a distance-to-segment cap test (the `k` /
`end_weight` term using `v_start_end` and `v_line_width`), but in the edge-id
pass that test does not reliably keep square-cap corner fragments out of the
buffers: such fragments still land in the edge-id buffer (wrong face id) AND in
the edge-id depth buffer (wrong depth). Because the pass owns its depth and the
nearest fragment wins per pixel, a stray cap fragment can occlude the correct
edge id, and the fill-composition match then picks the wrong line color (or
none) at that pixel.

The fix is to make the cap `discard` correct for the rounded shape AND
depth-affecting, so a discarded cap fragment writes neither id nor depth. The
fragment shader already has every input it needs: segment endpoints in
viewport-relative pixels (`v_start_end`) and line width (`v_line_width`). No
new inputs are required.

## Enable only the device features the backend uses

`src/erhe/graphics/erhe_graphics/vulkan/vulkan_device_init.cpp` builds the
`VkDeviceCreateInfo` feature chain by copying each feature from the value the
physical device reported (`set_*.feature = query_*.feature`) rather than
requesting the features the engine uses.
`VkPhysicalDeviceDescriptorIndexingFeatures` is the clearest case: all 20
fields are copied from the query while the backend depends on three
(`runtimeDescriptorArray`, `descriptorBindingPartiallyBound`,
`descriptorBindingVariableDescriptorCount`). `dynamicRendering` is enabled the
same way and is never used: the backend renders through `VkRenderPass` and
`vkCmdBeginRenderPass2` and never calls `vkCmdBeginRendering`.

Why that matters: the apparent dependency surface is wider than the real one,
so a reader cannot tell what is required; and a shader that relies on a SPIR-V
capability by accident runs on the development machine (because the matching
feature happened to be available and got enabled) and then fails
`vkCreateShaderModule` on hardware where it is not.

### Wanted

- A minimal required feature set stated explicitly, aborting (as a handful of
  features already do) when one is missing.
- Genuinely optional features enabled only where a concrete code path uses
  them, gated on the corresponding `Device_info` flag.
- Named fields set one by one instead of whole feature structs copied, with
  `dynamicRendering` left out until the backend moves to dynamic rendering.
