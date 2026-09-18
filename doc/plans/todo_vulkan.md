# Vulkan backend: enable only the device features the backend uses

Status: proposed

This plan extends `doc/vulkan_backend.md` ("Features enabled but not used by
the backend") with an explicit required-feature contract.

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

## Wanted

- A minimal required feature set stated explicitly, aborting (as a handful of
  features already do) when one is missing.
- Genuinely optional features enabled only where a concrete code path uses
  them, gated on the corresponding `Device_info` flag.
- Named fields set one by one instead of whole feature structs copied, with
  `dynamicRendering` left out until the backend moves to dynamic rendering.

This file exists because `doc/plans/vulkan_backend.md` is being written in
parallel; fold this entry into it.
