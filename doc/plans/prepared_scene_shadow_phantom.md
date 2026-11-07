# Phantom shadow caster with stored vertex streams

Status: in progress

Bistro loaded from stored GPU vertex streams (`ERHE_gpu_vertex_streams`: the
mesh items of a prepared run, doc/editor/prepared_assets.md "Prepared run")
renders with a wrong shadow on the Intel ICD: one spot light shades a region
of the cafe terrace that the same scene loaded from its soups lights
correctly. The findings below were made with the prepared glTF files that
Prepare (PC) / Prepare (Quest) of earlier builds wrote, which carried the
same streams inline; the editor writes no such file now, and the
reproduction is the prepared run that reads them from mesh items (not yet
re-run in that form). This document holds what is established, the
reproduction, and the open hypotheses.

## Reproduction

Headless Release Vulkan editor (`doc/agents/linux.md`, Intel ICD, Mesa ANV
25.2.8), run with `--no-scene --no-shader-analytics`. The phantom needs the
extra viewport windows: `config/editor/desktop_windows.json` with
`Viewport_window 2` and `Viewport_window 3` open (and `Hierarchy_window 2`,
`fly_camera`, `scene_control`, `shader_variants`). With the committed config
files the two files render byte-identical. With the extra windows but an
`editor_settings.json` stored at `_version` 12 instead of 14 (identical
settings in memory after loading) the phantom is also gone, so it depends on
GPU memory layout or timing, not on a setting. Under those conditions it is
deterministic: 12593 pixels differ on every run.

1. `open_scene` `res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf`,
   wait for `get_async_status` idle.
2. `save_scene` to `<scratch>/bistro_plain.glb`, add export settings
   (`set_scene_settings`) and `prepare_scene` (`target` `pc`; poll
   `get_save_status`): `<scratch>/bistro_plain.pc.erhe_prepared`.
3. Move `bistro_plain.glb` and its editor asset away, open the prepared asset
   (a prepared run, `load.cpu_mesh_data` `release` so its primitives load
   their stored streams), and open the moved `bistro_plain.glb` in the same
   session (`open_scene`, wait idle).
4. `render_scene_image` of each, `scene` the scene name, camera
   `{"eye":[-20,10,25],"target":[0,5,0],"up":[0,0,1],"fov_y_degrees":60,"near":0.1,"far":1000}`,
   960 x 540, `shader_debug` 30, `shadow_debug_light` `Spot.002`,
   `msaa_samples` 0. The plain file showed a slatted (lattice) shadow on the
   terrace; the prepared file a solid quad there (about 12600 pixels differ).

## Established

- With shadows off (`set_graphics_preset` `shadow_enable` false) the two files
  render byte-identical: geometry, materials and textures match; only shadows
  differ. Of the 8 shadow-mapped lights only `Spot.002` differs (`Spot.001`
  by 29 pixels).
- Both prepared targets show it (PC and Quest files alike), so it is not the
  BC texture work; the cause is the GPU vertex streams every prepared file
  carries: with `load.use_gpu_vertex_streams` false in
  `editor_settings.json` the PC file's shadow matches the plain file.
- The stored streams are correct: decoding each primitive's stored optimized
  `POSITION` and `TEXCOORD_0` (`ERHE_gpu_vertex_streams`, scale / offset from
  the hexfloat strings) against the core accessors gives errors at the
  quantization level only, for all 3852 primitives.
- The bucket path is correct: with `set_draw_lists_enabled` false both files
  render identical. With draw lists on, `set_culling_classification` `cpu`
  is correct too; only GPU classification (GPU-generated shadow commands,
  `Draw_list_scene::draw_list_gpu_chunks()`) shows the phantom. Occlusion
  culling off and `lod_enabled` false do not change it; the shadow map cache
  off does not change it.
- The caster: hiding the node
  `Bistro_Research_Exterior_Paris_Building_01_paris_buildi_1a23426` (opaque
  `MASTER_Wood_Painted3.003`, 566 vertices, 2583 indices; slatted shutters,
  the lattice of the plain file's shadow) with `set_item_property` `visible`
  false removes the phantom; showing it brings it back. Bisecting by hiding
  (via `batch`) converges; bisecting by `delete_nodes` does not (deletion
  changes the draw lists and allocations).
- Its draw-list entries (MCP `get_lod_info`, `get_draw_lists` with
  `mesh_name`): plain file `first_index` 33291, index pool buffer `0:1`; PC
  file `first_index` 3716916, index pool buffer `0:6` (stored-stream loading
  uploads the shared indices for both variants, so index data spans more
  pool buffers). Both: `index_count` 2583, variant `optimized`, level 0, no
  LOD chain.
- The draw-list entries are not stale: a temporary check in
  `Draw_list_scene::rebuild_all()` comparing each entry's `first_index`,
  `base_vertex`, variant and buffer set with a fresh `classify_primitive()`
  found 0 of 7576 stale, and a full rebuild does not remove the phantom.
- `get_cull_classification_check` reports 0 mask and 0 command mismatches
  (GPU commands vs the CPU command writer) also in frames that include the
  `render_scene_image` capture (30 views vs 29), run concurrently with it.
- Per job index buffers: `generate_gpu_commands()` builds one
  `m_gpu_indirect_count_index_buffers` table per frame for shadow and color
  jobs; `Render_command_encoder::multi_draw_indexed_primitives_indirect_count(set, job)`
  binds the job's buffer before `vkCmdDrawIndexedIndirectCountKHR`.

- It is specific to the Intel driver's GPU-generated indirect draws in
  ring mode. With the same config and files:
  - NVIDIA RTX 5070 (`VK_DRIVER_FILES` `nvidia_icd.json`): 0 pixels differ.
  - Intel ANV with the driconf environment override
    `generated_indirect_threshold=1000000` (generated draws off): 0.
  - Intel ANV with `generated_indirect_ring_threshold=100000000`
    (generated draws on, ring mode off): 0.
  - Intel ANV, ring threshold 100 or 400: 12593 (phantom). Ring threshold
    654, 655, 800, 1100, 1266 or 1267: 0. The draw lists' sizes change
    from run to run (the pixel result does not), so a threshold does not
    name one job. The ring mode of several count draws (max draw counts
    above about 400) seems to be needed.
- Vulkan validation finds nothing: synchronization validation reports 0
  hazards over the reproduction. With GPU-assisted validation the phantom
  disappears and nothing is reported.
- The erhe draw side is consistent: a temporary check in
  `Draw_list_scene::draw_list_gpu_chunks()` (job info list / chunk vs the
  drawn list, the published slot's list vs the drawn list, the set's index
  buffer vs the list's, `max_draw_count` vs the chunk, the job's entry
  begin) found 0 mismatches. Shadow passes draw only when the shadow map
  cache invalidates them, mostly at the `render_scene_image` capture.
- Zeroing every job's command slots past the GPU count in
  `cull_compact.comp` does not change the phantom: the driver does not draw
  from commands beyond the count.

## Open hypotheses

- A Mesa ANV defect in ring mode for `vkCmdDrawIndexedIndirectCount`.
  It shows when consecutive count draws bind different index buffers, which
  the prepared files cause by spreading index data over 7 pool buffers.
  The symptom fits: the shutter's vertices (its `base_vertex`) drawn
  through other indices. Mesa 23.1 fixed "anv: fix indirect draws VF cache
  tracking of index buffer"; an Intel vertex-fetch (VF) cache that keys on
  the low 32 address bits would explain the dependence on memory layout.
  No public report of this exact defect was found (searched 2026-11-03).
- An erhe defect that only ANV's ring mode exposes. Nothing found so far:
  the commands, counts and bindings check out, and the spec allows any
  `maxDrawCount` with a count buffer.

## Issue notes (for a driver report)

The notes an upstream report (Mesa GitLab, `anv` label) needs, collected in
one place.

### Environment

| Item | Value |
|---|---|
| GPU | Intel(R) Graphics (ARL), Arrow Lake integrated |
| Driver | Mesa ANV 25.2.8-0ubuntu0.24.04.4, Vulkan API 1.4.318 |
| OS | Ubuntu 24.04, Linux 7.0 kernel |
| Validation layer | LunarG SDK 1.4.363.0 |
| Correct on | NVIDIA RTX 5070 Laptop (proprietary driver 595.x), same machine |

### Symptom

A depth-only spot light shadow pass draws one caster wrong: an opaque mesh
of slatted shutters (566 vertices, 2583 indices) shades as
a solid quad covering its outline. The slats' gaps are filled, as if the
mesh's vertices (its `base_vertex` range) were drawn through other indices.
The color passes of the same frame draw the mesh correctly. A/B images: the
plain file's Spot.002 visibility (shader debug 30) shows the slatted
lattice; the prepared file shows a solid dark quad; in the lit image the
spot pool on the cafe terrace is dark.

### What the application does

Per shadow view, one render pass with one depth-only pipeline. For each draw
list (one per vertex format / index pool buffer / material class):

1. `vkCmdBindVertexBuffers` for the list's vertex streams.
2. `vkCmdBindIndexBuffer(buffer, 0, <the pool's index type>)`: the list's
   index pool buffer. Consecutive lists bind different buffers. The
   prepared file spreads index data over 7 pool buffers (`0:0`..`0:6`), the
   plain file over 2.
3. `vkCmdDrawIndexedIndirectCount(command_buffer, job_offset, count_buffer,
   job_index * 8, max_draw_count = job entry count, stride 20)`.
   `max_draw_count` ranges from 1 to about 1300. Commands and counts come
   from a compute dispatch earlier in the same command buffer
   (`cull_compact.comp`), followed by a memory barrier to `DRAW_INDIRECT` /
   `INDIRECT_COMMAND_READ`, shader storage access and transfer. Each command is `index_count`,
   `instance_count` 1, `first_index`, `base_vertex`, `first_instance`. The
   culprit's `first_index` is about 3.7 million.

Code: `Draw_list_scene::draw_list_gpu_chunks()` (`draw_list_scene.cpp`),
`Render_command_encoder::multi_draw_indexed_primitives_indirect_count()`
(`render_command_encoder.cpp`, the Vulkan impl in
`vulkan_render_command_encoder.cpp`), the job table in
`Draw_list_scene::generate_gpu_commands()` (`draw_list_scene_gpu_cull.cpp`).

### Driver configuration matrix

Driconf options set as environment variables for the editor process.

| Configuration | Pixels differing |
|---|---|
| ANV defaults | 12593 (phantom) |
| `generated_indirect_threshold=1000000` | 0 |
| `generated_indirect_ring_threshold=100000000` | 0 |
| `generated_indirect_ring_threshold=100` or `400` | 12593 |
| `generated_indirect_ring_threshold=654`, `655`, `800`, `1100`, `1266`, `1267` | 0 |
| NVIDIA ICD | 0 |
| ANV + Khronos validation with GPU-assisted validation | 0 (no messages) |
| ANV + Khronos validation with synchronization validation | 12593 (0 hazards) |

### Ruled out on the application side

- Command contents and counts: the `get_cull_classification_check` MCP
  tool reads back the GPU-written commands and counts and compares them
  with the CPU command writer: 0 mismatches, also in the capture frames.
- Bindings: a temporary draw-time check found 0 mismatches between the
  bound index buffer and the list's, the job and the drawn list / chunk,
  and the count and the chunk size.
- Command slots past the count: zeroing them in the compaction shader does
  not change the result.
- Data: the stored vertex streams decode to the source data at the
  quantization level; the CPU-command path and the bucket path, which use
  the same buffers with `vkCmdDrawIndexedIndirect`, draw it correctly.

### Before reporting

A self-contained reproduction (`erhe_graphics_gpu_tests`, deferred
2026-11-03 by the user): several count draws in one render pass, each with
its own index buffer and a max draw count of several hundred, compared with
ring mode on and off. Also re-test on the newest Mesa available.

## Next steps

1. (Deferred 2026-11-03 by the user.) Minimal reproduction in
   `erhe_graphics_gpu_tests` ("Issue notes", "Before reporting"). If it
   reproduces, it is a driver bug: report it upstream with the test.
2. Try a newer Mesa (25.3 or later) when one is available.
3. Only once the driver bug is confirmed: a `Device_info::workaround_*`
   flag gated on ANV (and the affected Mesa versions), per the AGENTS.md
   rule on driver workarounds (`doc/erhe/shader_workarounds.md`).
