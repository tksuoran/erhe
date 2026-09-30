# Port agfx GPU tests to erhe_graphics_gpu_tests: remaining work

Status: proposed

Extends [`erhe/graphics_test_coverage.md`](../erhe/graphics_test_coverage.md),
which describes the golden machinery ("Golden assertions") and lists every
test the suite runs, and [`graphics_tests.md`](graphics_tests.md). The
source is the agfx test suite reviewed in
[`reference/agfx_comparison.md`](../reference/agfx_comparison.md) (agfx
commit `d8ad38b`, `<agfx>/src/agfx/agfx_tests/`). The agfx tests whose
feature erhe has are in the suite; this plan holds the ones that wait on
an erhe feature, and the engine findings the port left open.

## 1. Rules for porting a test

- One erhe test per agfx test name, the agfx name quoted in the test's
  comment. Written against `Gpu_test` or a file-local fixture derived from
  it, shaders in GLSL inline, push constants as a UBO or a GLSL define,
  bindless handles as `Bind_group_layout` bindings.
- Every test keeps at least one analytic assertion and asserts a golden
  through `expect_image_matches_golden` or `expect_buffer_matches_golden`. A
  golden alone cannot catch a result that has always been wrong.
- Goldens are erhe's own, generated with `ERHE_GPU_TEST_UPDATE_GOLDENS=1`
  and reviewed by eye before the commit that adds them. One golden set
  serves every backend: positions and regions are stated in image space
  and converted with `texture_origin`, depths with `native_depth_range`.
- No agfx source is copied.
- A compute-produced image goes through a fullscreen fragment pass or an
  SSBO copied into a texture with `copy_from_buffer`, unless the test's
  subject is the storage image itself.
- Gates per commit: `erhe_graphics_gpu_tests` on `build_vs2026_vulkan_headless`
  passes with zero validation messages; on `build_tests` (OpenGL) every test
  passes or skips with a capability reason (the comparison-sampler cases
  under "Open findings" excepted); the coverage matrix gains the rows;
  `py -3 scripts/check_doc_links.py` is clean.

## 2. Not ported

Each row waits on the erhe feature named; the number is the
[`reference/agfx_comparison.md`](../reference/agfx_comparison.md) section 6
item that would add it. When that item lands, its tests come from this list
under the rules of section 1.

| agfx tests | Waits on |
|---|---|
| DrawMeshShader, DrawTaskMeshShader, DrawMeshIndirect(Multi), DrawTaskMeshIndirect(Multi), PipelineCacheMeshPs/MeshOnly/TaskMeshPs/TaskMeshOnly | Mesh and task shaders (item 12) |
| DispatchIndirect, and the count-buffer half of the indirect tests | Indirect dispatch and GPU draw count (item 3) |
| DrawPushConstants | User-facing push constants (item 4) |
| PipelineCacheCompute, PipelineCacheVsPs, PipelineCacheVsOnly | Pipeline cache persistence (item 1). erhe's form of the feature is a cache file loaded at device creation, not a user-visible blob, so its test is "second device creation reports cache hits", written with item 1 |
| ComputeQueueToRenderQueue, CopyQueueToRenderQueue, QueueChainCopyComputeRender | Async transfer and compute queues (item 9) |
| AliasHeapTransients, AliasBufferOverlap, AliasHazardOrdering | Placement heaps and aliasing barriers (item 11) |
| RTUpdateBLAS, RTCompactBLAS, RTCopyAccelerationStructure | Acceleration structure update, compaction and copy (item 10) |
| RaytraceAABBOneGeometry, RaytraceAABBMultipleGeometry, RaytraceMixedGeometryTriangles/AABBs | AABB (procedural) geometry in `Acceleration_structure`; no item, add one if a consumer appears |
| RTInstanceOpacityOverridesGeometry, RTTlasOpacity | Per-instance force-opaque / force-non-opaque flags; no item |
| RTScratchBufferReuse, RTBuildWithOffsetScratchBuffer | User-provided scratch buffers; erhe owns scratch internally, by design |
| ComputeTextureWrite2DArray, ComputeTextureWrite3D | `set_storage_image` for `image2DArray` / `image3D`; small, add with the first consumer |
| DrawWireframe | `fillModeNonSolid`, forced off in `vulkan_device_init.cpp`; the coverage matrix records polygon mode as not testable |

Not applicable to erhe: BufferViewHandleNotNull, SamplerHandleNotNull,
TextureViewHandleNotNull, TLASAddressNotNull (bindless handle numbering),
DeviceInfoDriverVersionNotEmpty (`Device_info` is covered by
`test_device_caps.cpp`), ComputeTextureWrite (subsumed by
`test_storage_image.cpp`).

## 3. Open findings

Engine issues the port exposed that are not fixed.

- Vulkan `copy_from_buffer` and texture-to-texture `copy_from_texture`
  (`vulkan_blit_command_encoder.cpp`) transition the destination subresource
  from `VK_IMAGE_LAYOUT_UNDEFINED`, which permits the driver to discard its
  contents; region copies over a seeded destination keep the other texels
  only by driver leniency. Using the tracked layout as `oldLayout` needs
  per-subresource layout tracking, since the texture tracks one layout for
  all levels and layers. The copy tests would catch a discarding driver.
- OpenGL on the AMD Radeon 890M driver 26.8.1.260810: after a comparison
  sampler draw with `never`, later draws of the identical shader source with
  a different compare function read as `never` although the sampler and
  program state queried at the draw is correct (`Sampler_comparison_test`,
  six cases fail on that driver; see
  [`erhe/graphics_test_coverage.md`](../erhe/graphics_test_coverage.md)
  "Known gaps"). Not reproduced on Vulkan.
- `scripts/gpu_test_report.py` previews a `.pfm` golden clamped to [0, 1],
  so an HDR golden with values above 1 shows white; the preview needs a
  tone map or an exposure control.
