# Memory usage after a prepared Bistro load (2026-10-25)

This report measures where CPU and GPU memory goes once the prepared Bistro
(`bistro_prepared.glb`, written by `save_scene` with prepared mesh data and
BVHs, `doc/plans/gltf_fast_load.md`) has loaded and the editor is idle. It
ranks what could be dropped. Desktop and Quest 3 are measured side by side.
The figures come from MCP `get_memory_usage`
(`doc/editor/reloadable_asset_loads.md` "Measuring"). The ranking feeds the
plan's follow-up item 3, which drops CPU mesh memory after GPU upload.

## Setup

- **Editor builds:** `--no-scene` start, which gives an empty "Scene 1" with
  a default camera. Then MCP `load_scene` of the prepared file. The
  "after" sample is taken 5 s (desktop) or 8 s (Quest) after
  `get_async_status` reports idle.
- **Desktop:** headless Vulkan, Release, Windows, AMD iGPU (device-local
  memory is system memory). The drivers are under the gitignored `logs/`
  directory, which is not committed.
- **Quest 3:** release APK, off-head automated run, Vulkan. The headset
  stays homed in Scene 1, so Bistro is resident but not drawn (see "Open
  items").
- **Units:** MB = 10^6 bytes. CPU figures are allocated capacities.
- **Objects:** 3852 distinct primitives (one render shape each), all with
  prepared data and a stored BVH, 0 Geometry, 0 collision shapes, and 393
  scene textures.

## Totals: process view against tracked owners

| | Desktop | Quest 3 |
|---|---|---|
| CPU heap before load (Windows heaps allocated / bionic mallinfo in use) | 115 | 89 |
| CPU heap after load | 1182 | 1232 |
| **CPU heap added by the load** | **1067** | **1143** |
| tracked: CPU mesh data (`cpu_mesh`) | 773 | 774 |
| tracked: retained encoded images (`cpu_images`) | 208 | 208 |
| **CPU unexplained** (scene graph, materials, draw lists, content library, allocator overhead) | **86** | **161** |
| GPU allocator blocks before / after (VMA, device-local) | 1311 / 3863 | 570 / 2986 |
| GPU allocations after (textures + buffers) | 3466 | 2629 |
| GPU free space inside allocator blocks | 397 | 357 |
| Resident after: working set / VmRSS | 1409 | 1253 (RssAnon 1165) |
| Commit after: private bytes / dumpsys TOTAL PSS | 5876 | 4479 |
| **Peak: peak working set / VmHWM** | **4108** | **3790** |
| Peak CPU heap during the load (sampled) | 3224 | 3386 |

Quest `dumpsys meminfo` after the load:

- Native Heap: 1159
- GL mtrack: 2981. This matches the VMA block bytes, so on Quest the GPU
  allocations are the "Graphics" part of PSS.
- EGL mtrack: 185 (swapchain and compositor images)
- Code: 18

On the desktop iGPU, private bytes are the CPU heap commit (1507) plus the
VMA blocks (3863) plus about 500 of driver, code and stacks. GPU memory does
not appear in the working set.

## CPU mesh data per owner

Every primitive is counted once. A shared soup or prepared data counts once.

| Owner (field in `cpu_mesh.render_shapes`) | Desktop | Quest | Who reads it after the load |
|---|---|---|---|
| Triangle soup vertex bytes | 249 | 250 | See the soup reader list below |
| Triangle soup index bytes | 50 | 50 | (same as the vertex bytes) |
| Prepared topology (ERHE_mesh_topology vertex points) | 16 | 16 | Building the Geometry on demand (edits, Geometry-based tools) |
| Raytrace BVH nodes | 114 | 114 | CPU ray casts: pointer hover, picking, MCP ray queries |
| Raytrace primitive ids | 33 | 33 | (same as BVH nodes) |
| Raytrace precomputed triangles (bvh backend) | 199 | 199 | CPU ray casts. A second copy of every triangle's positions in the intersection layout |
| Raytrace vertex / index copies (`Primitive_raytrace` Cpu_buffers) | 97 | 97 | Only `commit()` builds from them. Afterwards only the developer-mode Properties "Raytrace Buffer Mesh" and a collision bounding box read them |
| Raytrace triangle-to-facet map | 17 | 17 | Hover facet lookup. For a prepared soup this is the identity (triangle t is facet t), stored as a vector |
| Stored / prepared BVH (not yet moved into the raytrace) | 0 | 0 | Moved into the raytrace at commit; nothing is held twice |
| Element mappings | 0 | 0 | - |
| **Total** | **773** | **774** | |

The CPU mesh copies that exist after the load:

- **Optimized variant:** the `optimized_render_shape` of all 3852 primitives
  exists. It shares the render shape's soup, so it holds no bytes of its
  own: the prepared core is already optimized.
- **Soup readers:**
  - glTF and USD export, which writes the core from the soup.
  - `Primitive_shape::make_geometry()`, which builds Geometry on demand
    from the soup and the topology.
  - The vertex-format and color rebuild in `App_scenes` (`app_scenes.cpp`).
  - The mesh optimizer.
  - The prepared-data export (`Prepared_primitive_export::retained_core`).
  - A raytrace build when there is no stored BVH.
  - The mesh `geometry_count` properties (vertex / triangle counts in the
    UI).
- **Raytrace:** it holds three representations of the same triangles: the
  vertex / index copies, the precomputed triangles, and the BVH over them.
- **Not held:**
  - No CPU copy of the GPU vertex / index buffers. `Buffer_mesh` holds only
    ranges into the mesh pools.
  - No Geometry.
  - No collision shapes and no Jolt bodies (a fixed 10 MB temp allocator).
  - No retained glTF buffers: `Gltf_data` keeps no buffer bytes, and the
    fastgltf asset dies with the parse.
  - No decoded pixels after residency: `decoded_pixel_bytes` is 0.

## Images

| | Desktop | Quest |
|---|---|---|
| Retained encoded source streams (KTX2) for byte-exact export | 208 | 208 |
| Scene textures, GPU: `format_bc5_unorm` (190) | 923 | 923 |
| Scene textures, GPU: `format_bc7_srgb` (203) | 884 | 884 |
| All textures, GPU (incl. editor, render targets, shadow maps) | 2794 | 1962 |

- **Quest transcodes to BC7 / BC5 too:** the device reports BC7 support,
  and `query_gltf_device_options()` prefers BC7 over ASTC. Basis transcodes
  to 1 byte per pixel either way, so ASTC 4x4 (follow-up item 4) would not
  reduce texture memory. It could only save the transcode time.
- **Desktop holds 988 MB of non-scene textures, Quest 156.** Desktop
  textures other than the 393 scene textures grow from 564 (before the
  load) to 988. The difference is the Bistro viewport's shadow maps
  (8 x 2048^2 + 4 point cubes at 1024, d32) and its render targets: the
  desktop opens a second viewport beside Scene 1's. Quest has no desktop
  viewport.

## GPU buffers

| | Desktop | Quest |
|---|---|---|
| Mesh pools capacity / used | 662 / 646 | 717 / 647 |
| All buffers (`buffers.byte_count`) | 668 | 727 |
| Acceleration structures (GPU BLAS) | 0 | 0 |
| Ring buffers | 4 | 8 |

The mesh pools carry the acceleration structure build-input usage, which is
why `buffers` files them under that category. No BLAS exists after the load.

## Transient peak during the load

The peak exceeds the idle RSS by about 2.6 GB on both platforms.

**The CPU heap curve.** It peaks right after `parse_gltf` (Quest: about
9.8 s into a 15.7 s load, 3386 MB heap) and then falls as image residency
uploads the decoded pixels and frees them, about 1.8 GB over the next 6 s.
At the peak, the heap holds:

- The final mesh and raytrace data (about 0.8 GB) and the retained images
  (0.2 GB).
- The decoded, transcoded pixels of every image at once (up to the 1.81 GB
  of scene texture bytes). `parse_gltf` decodes all images in parallel
  before residency starts uploading them at a per-frame byte budget.

**An earlier, smaller transient during the parse.** It is not the peak
today:

- `GltfDataBuffer::FromPath` reads the whole 664 MB .glb into the heap.
- fastgltf then copies the GLB BIN chunk into a second array
  (`sources::Array`, fastgltf.cpp `parse` of the binary chunk), which
  briefly doubles that.
- The desktop heap reaches 742 MB 0.5 s into the load, about one file's
  size above the baseline.

## Ranked optimization candidates

| # | Candidate | Saves (idle / peak) | Cost | Notes |
|---|---|---|---|---|
| 1 | Bound the decoded pixels: decode images inside residency with an in-flight byte budget instead of all in `parse_gltf` | peak about -1.5 GB on both | medium | Makes VmHWM about idle + file. The likely cause of the foreign load's lmkd kill on Quest (step 9) |
| 2 | Free the raytrace vertex / index copies after `commit()`; store the identity facet map as a flag | idle -114 (97 + 17) | low | Properties debug view and bounding box read from the soup or the Buffer_mesh bounds instead |
| 3 | Drop CPU soups after GPU upload, reload from the source glTF on demand (follow-up item 3) | idle -315 (soups 299 + topology 16) | high | Every soup reader in the list above needs an on-demand path; export and Geometry are the big ones |
| 4 | Make the CPU raytrace lazy for prepared primitives (keep the stored BVH in the file, build the raytrace on first ray query) or drop the precomputed triangles in favor of soup-indexed intersection | idle -346 (nodes + ids + precomputed) lazily, or -199 (precomputed only) | medium | On Quest the CPU raytrace serves controller hover / picking. A lazy build costs a first-hover stall per primitive. Composes with item 3 only if the raytrace keeps its own positions |
| 5 | Drop the retained encoded image streams; re-read the bytes from the source file at export | idle -208 | low-medium | Needs the source path and byte range per image, and a fallback when the source file changed |
| 6 | Read the .glb without the double copy: fastgltf `mapCallback` into one allocation, or a memory-mapped `ByteView` | parse-time transient about -664 | low | Becomes the peak once item 1 lands |
| 7 | VMA block slack | GPU about -0.36 GB (Quest) | unknown | 357 MB of free space inside blocks after the load. Investigate block sizes and empty-block retention |
| 8 | Mesh pool slack | GPU -70 (Quest) | low | 717 capacity against 647 used |

Items 2 to 5 together remove about 0.98 GB of the 1.07 to 1.14 GB the load
adds to the idle CPU heap. Item 1 alone removes most of the gap between
VmHWM and idle RSS.

## Open items

- **Bistro resident, not rendered:** in these runs the headset showed
  Scene 1, so the Quest figures measure Bistro as resident, not rendered.
  The headset can now be moved to the loaded scene (`close_scene` of
  Scene 1, or MCP `set_headset_scene`; `doc/editor/tools.md` "Headset
  scene").
- **Unexplained CPU remainder:** 86 MB on desktop and 161 MB on Quest. It is
  not broken down by owner (scene graph, materials, Draw_list_scene,
  content library, allocator overhead).
- **Pipeline and shader caches:** not measured.
