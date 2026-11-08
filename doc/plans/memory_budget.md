# Memory budget for scene loads

Status: in progress

A scene load estimates the texture and mesh memory it will make resident and,
when the texture estimate would exceed the texture memory budget, plans the
textures to fit: transcode to a block-compressed format while loading, and
downscale the largest textures until the scene fits. Requested by the user
2026-11-02 after the Sponza fetch (`doc/editor/asset_fetch.md`) OOM-killed
the editor on a 32 GB machine.

## User decisions (2026-11-02)

- D1. At load start, estimate whether the scene fits. Over the texture
  budget: transcode textures to a compressed format during loading and, if
  still over, downscale (at least the largest) textures, picking sizes so the
  scene fits the texture budget.
- D2. Defaults: 3 GB texture memory, 1 GB mesh memory.
- D3. Each budget has an absolute and a fractional limit; either can be 0
  (ignored); the effective limit is the minimum of the non-zero ones.
- D4. Analyze where the rest of the memory went (section "Analysis").

## Analysis: Sponza load, 2026-11-02

Setup: headless Release Vulkan editor on the Intel iGPU (device-local memory
is system memory, driver-reported device-local budget 23.9 GB), launched in a
`systemd-run --user --scope -p MemoryMax=16G -p MemorySwapMax=0` scope;
`open_scene res/editor/assets/sponza/sponza.gltf` (host file instancing the
Intel base scene and curtains as prefabs). The cgroup's `memory.stat` was
sampled every second (MCP did not answer: the main loop was stalled).

| t (s) | cgroup | anon (CPU heap) | shmem (GPU objects) |
|---|---|---|---|
| 1 (before open) | 1.93 GB | 0.12 GB | 1.66 GB |
| 10 | 3.33 GB | 0.40 GB | 1.93 GB |
| 19 | 11.09 GB | 1.00 GB | 8.40 GB |
| 22 | 17.18 GB (cap; killed) | 1.21 GB | 14.87 GB |

The CPU heap stays near 1.2 GB; the growth is GPU memory (on this driver GPU
buffer objects are unevictable shmem). The textures account for about half:

- Textures: 80 PNG images, all 4096 x 4096 (53 RGB, 27 RGBA, 8 bit), uploaded
  as RGBA8 with a generated mip chain: 80 x 89.5 MB = 7.2 GB.
- Staging: every 4K image (64 MiB) is larger than the image transfer's
  staging ring, so `Image_transfer::upload()` creates a one-shot staging
  buffer per image (72 log lines "exceeds the staging ring"). The prefab load
  runs on a worker in blocking-drain mode and waits for the copy, but
  `~Buffer_impl` (Vulkan) never frees at once: it queues the free on the
  device frame's completion handler. The main loop was stalled (render thread
  in `submit + end_frame` for over 20 s), so no frame completed and the 72
  buffers piled up: 72 x 64 MiB = 4.6 GB. The comment at that site ("the
  copies are flushed immediately so the buffer can be released right away")
  states an intent the destructor does not honour.
- Baseline before the open: 1.66 GB shmem (VMA device-local blocks 1.59 GB,
  0.79 GB of it allocated: the empty editor's render targets, mesh pools,
  acceleration structure build input 239 MB).

7.2 + 4.6 + 1.7 = 13.5 GB of the 14.9 GB shmem; the remainder is the mesh
data of the two prefabs (140 MB + 73 MB `.bin`) and the frame's transient
buffers.

After step 1 (run under a 6 GB cap, sampled every 0.2 s, killed at the cap
after 30 images): no oversize staging, CPU heap 0.4 - 1.0 GB, and shmem grows
in 0.27 GB steps, one per two images - 127 MB per 4K texture instead of
89.5 MB. The steps are VMA's default 256 MiB device-local blocks: two
89.5 MB textures fit a block, a third (268.4 MB with alignment) does not, so a
third of every block stays empty. A texture below VMA's dedicated-allocation
threshold (half a block) is never given its own allocation. Projected over
the 80 textures: about 10.2 GB instead of 7.2 GB.

After the texture plan and the finalize admission (20 GB cap, 46 s to
settled): content textures 2.7 GB (budget 3 GB), GPU mesh memory 5.85 GB used
(about 1 KiB per triangle over 5.9 M triangles: base and optimized variants,
expanded fill, edge lines, points), CPU heap 5.2 GB in use (soups 0.28 GB,
raytrace 0.53 GB, element mappings 0.1 GB, and about 4 GB of `Geometry` for
455 primitives that `get_memory_usage` does not count). The full editing
build of every mesh, not the BVH, dominates what a load keeps.

## Build order

1. **Oversize uploads through the ring (root cause of the 4.6 GB).** Built.
   In blocking-drain mode an image larger than the staging ring is uploaded
   in bands of whole (block) rows, each through the ring, flushing when it is
   full; no one-shot staging buffer remains there. Frame-recording mode keeps
   its one-shot buffer for an image larger than the upload staging cap: it is
   granted only while nothing else is staged and the frame loop that records
   it also completes it. Verified by `erhe_gltf_gpu_tests`
   `Image_transfer_gpu_test` (every level read back through a 4 KiB ring) and
   the probe run (0 oversize staging buffers).
2. **Large textures in dedicated allocations.** Built. A Vulkan image of
   64 MiB or more (a quarter of VMA's default 256 MiB block,
   `estimate_texture_byte_count()`) asks for a dedicated allocation. Each
   dedicated allocation takes one of the driver's `maxMemoryAllocationCount`
   slots (often 4096), so `Device_impl` grants at most an eighth of them; past
   that, large images share blocks again (logged once). The grant returns
   when the image is freed. Measured: 102 MB per 4K RGBA8 texture (was 127 MB;
   the rest over 89.5 MB is the driver's image size requirement).
3. **Budget settings.** Built. `Load_config` v10:
   `texture_memory_budget_mib` (default 3072), `texture_memory_budget_fraction`
   (default 0), `mesh_memory_budget_mib` (default 1024),
   `mesh_memory_budget_fraction` (default 0). A fraction is of the device-local
   budget `Device::get_memory_budget()` reports (system memory on
   unified-memory GPUs, VRAM on discrete ones). `get_load_memory_budgets()`
   (`src/editor/assets/load_memory_budget.cpp`): the smaller of the non-zero
   limits; both 0 = unlimited. MiB, not bytes: the codegen `Int` is 32 bit.
4. **Estimate.** Textures built: `Gltf_image_residency::plan_textures()` reads
   each requested image's header (a 64 KiB prefix, the whole stream when the
   prefix does not hold it; a KTX2 whose level data ends past the prefix, as
   `erhe::graphics::parse_ktx2_prefix()` reports, is opened from the whole
   stream only) before the first decode and sums the texture bytes
   as loaded (single-level RGBA8 with its generated mip chain; containers as
   stored). A load plans against the texture budget minus the content textures
   already resident: `Texture_create_info::memory_class`
   (`Texture_memory_class::content` for glTF images) feeds
   `Texture::Memory_statistics::content_byte_count`. Mesh estimate: open.
5. **Texture plan.** Built. Over budget: every single-level RGBA8 image is
   encoded to `Gltf_device_options::block_encode_format` (BC7 when the device
   samples BC7 in both color spaces, else ASTC 4x4, else none), then the
   largest planned texture is halved repeatedly (ties: lowest image index)
   while it stays at least 256 texels on its short side; containers with mips
   drop top levels instead. The plan is logged ("Texture plan: ...").
   It reads each image's size with `Image_loader::probe_prefix()` on the
   first 64 KiB, and the whole stream only when the header ends past them;
   an image whose header is malformed is logged once and the decode skips it
   (`Gltf_decoded_image::header_malformed`).
   Sponza base scene: 72 images, 6144 MiB as loaded, budget 3072 MiB ->
   72 encoded to BC7, 0 downscaled, 1536 MiB.
6. **Encoders.** Built: `erhe::graphics::reduce_rgba8_image()`
   (`image_encoder.{hpp,cpp}`): box-filter halving (sRGB averaged in linear
   light), CPU mip chain, BC7 via `basisu_bc7e_scalar.cpp` (ultrafast preset)
   and ASTC 4x4 via astc-encoder (fastest preset; the build no longer defines
   `ASTCENC_DECOMPRESS_ONLY`). Tests: `erhe_graphics_tests` `Image_encoder`.
   Cost on this machine, 1024 x 1024 with mips, one thread: BC7 236 ms,
   ASTC 746 ms; the Sponza base scene took about 95 s to load with every
   texture encoded - hence step 7.
7. **Encoded texture cache.** `erhe_gltf/image_reduction_cache.{hpp,cpp}`.
   A decode task with a reduction for a single-level RGBA8 image looks the
   result up before decoding; a hit replaces both the decode and the encode, a
   miss stores the result after the reduction. Key: the encoded source
   stream's content (FNV-1a, so a moved or copied asset still hits), the
   reduction, the color space and an encoder tag plus format version (bumped
   with any encoder, preset or filter change). Entry
   `<key>.erhetex` in `cache/texture_encode/` (editor,
   `make_gltf_device_options()`): a header (magic, version, key, format,
   extent, level count, row stride, payload size) and the encoded mip chain
   largest-first. A missing, corrupt or mismatched entry is a miss; an
   unwritable directory skips the store. A store writes a per-thread
   temporary file and renames it into place, so concurrent decode tasks and
   readers never see a partial entry. The decode slot still reserves the
   decoded size on a hit, which keeps the reservation independent of the
   cache state.
8. **Integration.** Built for the glTF load paths: Asset_manager loads, prefab
   loads, the synchronous import paths (`make_gltf_device_options()`); the XR
   controller models keep `query_gltf_device_options()` (no budget).
9. **Deferred finalize admission.** Built. The per-mesh deferred finalize
   tasks (`async_raytrace_kickoff_operation.cpp`: raytrace BVH, `Geometry`,
   full buffer build) wait in a main-thread queue and start only while a lease
   of the load memory budget (`Memory_budget_kind::finalize`) sized
   1 KiB per triangle fits; a finishing task releases its lease and pumps the
   queue through the scene commit queue (no worker ever blocks on the budget;
   BVH builds run on their own thread pool). Pending entries hold weak
   references and are dropped when their scene closes. This bounds the
   concurrent working set only: what a finalize keeps is measured below.
10. **Mesh budget: on-demand full builds.** Built. `finalize_imported_meshes()`
    (`get_import_full_build_policy()`) estimates what the deferred full builds
    of the import's non-prepared primitives would add to GPU mesh memory
    (1 KiB per triangle, measured on Sponza) on top of `Mesh_memory`'s used
    bytes. Over the mesh budget (user decision 2026-11-02: defer and release
    soups), each such render shape gets `Full_build_policy::on_demand`
    (`erhe::primitive::Primitive_shape`): no optimized variant and no
    `Geometry` at load, the deferred finalize builds only the real raytrace -
    from the soup (identity facet map), not through a `Geometry` - and the
    commit releases the soup. `Primitive::has_on_demand_full_build()` (prepared
    or on-demand) is what the finalize and `request_prepared_full_builds()`
    test, so selecting a mesh, or a viewport style that shows edge lines,
    queues its full build, which reads the soup back from the file. Every
    editor glTF parse from a file now records soup sources (prefab templates
    included), so the release does not depend on `load.cpu_mesh_data`.
    Sponza (5.9 M triangles, 8 GB cap): settles in 35 s; CPU heap in use
    0.66 GB (was 5.2 GB), GPU mesh memory 0.39 GB (was 5.85 GB), all 448 soups
    released; selecting `curtain_01` reloads its soup (9.6 MB) and builds its
    `Geometry`; the base color render shows the base scene and the curtains.
    Intel's Sponza glTF ships its 24 lights at intensity 0, so the lit render
    is black.
11. **Reporting.** The Loading section and the log show estimate, budget and
    plan; MCP `get_memory_usage` reports budgets, the content texture bytes,
    the last texture plan and the `Geometry` bytes (`cpu_mesh` counts
    Geometry objects but not their bytes).

## Open

- Per-load planning: a scene that opens several prefabs plans each load
  against what is left when it starts, so the first load is favoured.
- Default fractions (D3 leaves them to the implementation): 0 = absolute
  limits only, until measured on Quest.
