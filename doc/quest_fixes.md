# Quest-branch follow-up fixes

Fix plan for the non-obsolete findings in `doc/quest_review.md`. Issues that the `Forward_renderer` -> `Draw_list_renderer` swap retires (marked `[OBSOLETE]`/`[OBSOLETE in part]` in the review) are out of scope here -- see `doc/draw_list_renderer.md` for that work.

Each item names file:line, the bug, the fix, and the verification hook. Severity is the `doc/quest_review.md` label.

## Open issues

### B. Vulkan / Metal / shader infrastructure

B2a-perf. **[DEFERRED - perf measurement first] Vulkan / Metal pipeline ctor builds the GPU pipeline at construction** -- `src/erhe/graphics/erhe_graphics/vulkan/vulkan_render_pipeline.cpp`, `src/erhe/graphics/erhe_graphics/metal/metal_render_pipeline.cpp`. The ctor resolves `create_info.shader_stages_handle->shader_stages()` and immediately calls `vkCreateGraphicsPipelines` / `newRenderPipelineState`, baking the result into the impl. With `Shader_stages_handle`'s timing contract now implementation-defined (see B2a closed) this is no longer a correctness issue; the open question is cold-start cost on Quest where every pipeline built during editor init compiles up-front. **Fix (if justified by measurement):** defer pipeline build to first bind, mirroring the dynamic path already present in `vulkan_render_command_encoder.cpp:87-438` (hashes runtime state, reuses `device_impl.get_cached_pipeline(hash)`). Metal would cache an `MTL::RenderPipelineState` on the impl and resolve at first encoder bind. **Verify before doing anything:** measure editor-init shader-module + VkPipeline creation cost on Quest with validation layers off; only invest if init time is meaningful.

B3. **[DEFERRED - member-init constraint] Metal silently swallows wide_lines failure** -- `src/editor/renderers/programs.cpp:66-68`. Geom shader unsupported on Metal. **Fix:** gate the wide_lines `sv_key` registration on `use_compute_shader == false`, mirroring the pre-commit state. **Verify:** Metal build no longer logs shader-compile failures during init.

B12. **[DEFERRED - Draw_list_renderer Phase 2] Removed runtime sampler-presence checks risk bindless UB** -- `src/editor/res/shaders/standard.frag` (commit 7704e198 removed `if (normal_texture.x != max_u32)` guards; now gated by build-time `#ifdef ERHE_USE_*_TEXTURE`). Combined with Forward_renderer's OR-merge bucket key (obsoleted by the swap) the hazard fires today on Vulkan bindless. **Fix:** post-swap, `Draw_list_renderer`'s per-entry variant resolution makes the matching correct; add `ERHE_VERIFY` at draw-list-entry build time so any future mismatch trips loudly. **Tie-in:** `doc/draw_list_renderer.md` Phase 2 (already cross-referenced).

### D. Multiview / XR

D6. **[DEFERRED - needs multiview-MSAA root-cause] 1-MSAA on OpenXR is a workaround** -- `config/editor/graphics_presets_openxr.json:5`. Underlying multiview-MSAA issue not named or filed. **Fix (CLAUDE.md no-band-aid):** root-cause the multiview-MSAA failure, or remove the unreachable multiview-MSAA code paths. At minimum, file an issue and reference it in the config comment.

D7. **[DEFERRED - needs Command_buffer API plumbing] GL backend drops end_render_pass debug sub-scope** -- `src/erhe/graphics/erhe_graphics/gl/gl_render_pass.cpp:817-822`. Comment justifies the loss because `end_render_pass()` has no `Command_buffer&`. **Fix:** plumb a `Command_buffer&` through.

### E. Build / config / docs / dead-code

E3. **[DEFERRED - needs prewarm-orchestrator extension] `Device::warmup_render_pipeline` is dead code** -- `src/erhe/graphics/erhe_graphics/device.cpp:121-126, device.hpp:351`. Comments in `forward_renderer.hpp:131` and `prewarm.hpp:22` advertise it as Phase-2's VkPipeline-cache home, but `prewarm_all` never calls it. **Fix:** wire `prewarm_all` to call it for each variant the renderer's prewarm iterates (Phase-2 win); update `prewarm.hpp` comment.

E11. **[DEFERRED - codegen-generated migrations from added_in markers] `config/editor/erhe_graphics.json` `_version` 2 -> 4 without migration** -- `_version` skipped 3, no migration logic. **Fix:** add the missing `migrate_v3_to_v4` stub (and `migrate_v2_to_v3` if v3 was meant to land between commits). Per CLAUDE.md, only bump when migrating; the bump must come with code. **Do not** roll the field back to v3 -- the in-tree config is already at v4, and downgrading is gratuitous churn even if users' writable copies have not yet been migrated. Ship the migration stub instead.

#### Dead-code subsystems remaining from E13

Per CLAUDE.md, each in its own commit so `git blame` stays useful:

- **Renderers**: `Programs::load_programs`, `programs_load_task`, `Programs::m_handles_by_name`, `Programs::get_multiview` (`src/editor/renderers/programs.cpp:156-218`).
- **Graphics buffers**: `Buffer_pool::add_existing_block` + the "manual mode" ctor (`src/erhe/graphics/erhe_graphics/buffer_pool.{cpp,hpp}`).
- **Debug renderer**: `padding0_offset` (`src/erhe/scene_renderer/erhe_scene_renderer/debug_renderer_bucket.cpp` and header).
- **Mesh memory**: deprecated `vertex_buffer_size` / `index_buffer_size` config knobs (`src/erhe/scene_renderer/definitions/mesh_memory_config.py`).

### F. Commit hygiene

F1. **[DEFERRED - requires user authorization for history rewrite] Commit 8f90a2b0 message claims an OpenXR-instance change not in the diff.** **Fix options (in order of preference under CLAUDE.md):**

- (a) **Default:** land a follow-up commit that does the actual change and references 8f90a2b0 in its message. CLAUDE.md prefers creating new commits over amending; this is the only option that does not rewrite history.
- (b) If the change was an accidental drop and the original code can be recovered, the follow-up commit is the restore.
- (c) Rewriting the original commit message (via `git commit --amend` on tip or interactive rebase mid-branch) is destructive history rewriting and is blocked by CLAUDE.md's git-safety protocol -- both the "never run destructive git commands unless the user explicitly requests them" rule and the explicit "Never use git commands with the `-i` flag (like git rebase -i ...)" prohibition apply. Do not pick (c) without an explicit, scoped user authorization for *both* history rewriting and the `-i` flag.

Note: the branch's history was subsequently re-collapsed via the `git-history-cleanup` skill (user-authorized), so `8f90a2b0` no longer exists on the cleaned-up branch. Re-evaluate whether the original missing OpenXR-instance change still needs a follow-up commit.

### Carry-over follow-ups from partial-done fixes

- **A5 viewport teardown:** floor brush / shape / material covered by `Scene_builder_floor_resources_operation`; viewport teardown still requires a Scene_views teardown API that does not exist yet.
- **D4 rename:** stale comment dropped, `headset_config.py` `short_desc` fields filled. Renaming `Headset_config.depth` -> `composition_depth_layer` and `swapchain_depth` -> `swapchain_depth_attachment` deferred to avoid churn.
- **M2 codegen serializer:** glTF import wires `KHR_materials_unlit` -> `bxdf_model = unlit`. Codegen JSON serialization of the three new `Material_data` fields stays a follow-up because the material is not currently codegen-managed.
- **E1 SHA256:** `validationLayerVersion` is now an input on `fetchVulkanValidationLayer`; the `-Pvulkan_validation_skip` branch explicitly deletes any staged .so. SHA256 verification of the downloaded zip is still TODO.
- **E4 quad-view sourcing:** prewarm.cpp documents the hard-coded `view_count=2` as Quest-stereo specific; a Varjo quad-view (view_count=4) still needs `max_view_count` sourced from `Headset_view` / `Xr_session` at startup.

### Coupling and gating (open-issue dependencies)

- A11 (closed) is a prerequisite to retiring shadow_renderer (`doc/draw_list_renderer.md` Phase 6) cleanly.
- B12 is a Phase 2 deliverable of `Draw_list_renderer`, not a standalone fix.
- E13.Renderers (`Programs::load_programs` / `programs_load_task` / `m_handles_by_name` / `get_multiview`) survived B5 + B14 because deleting the legacy `Variant_handle` members shrank the per-name map but did not remove the name-keyed lookup path; ship it independently.

## Closed issues

### A. Renderer-agnostic correctness (high + med)

A1. **[DONE] camera_buffer trailing bytes uploaded uninitialised** -- `src/erhe/scene_renderer/erhe_scene_renderer/camera_buffer.cpp:122-140`. Single-camera `update()` writes one entry into a `block_size = entry_size * max_view_count` slice; cube_renderer / texel_renderer paths upload garbage in `cameras[1..N-1]` when multiview is on. **Fix:** memset trailing bytes after `write_camera_entry`, or shrink `block_size` to `entry_size` on the single-view path. **Verify:** RenderDoc capture under Quest multiview shows zero in `cameras[1]` for single-camera updates.

A2. **[DONE] ERHE_GRAPHICS_API_NULL typo breaks headless** -- `src/erhe/graphics/erhe_graphics/render_pipeline.cpp:12`. CMake defines `ERHE_GRAPHICS_API_NONE`. **Fix:** rename to `_NONE`. **Verify:** configure with `ERHE_GRAPHICS_API=none` and confirm `null_render_pipeline.hpp` is included.

A3. **[DONE] Init_status_display thread-safety race** -- `src/editor/init_status_display.cpp:57-64` (set_line), `:163` (poll_events), `:294-425` (render_present_xr); driven by `editor.cpp:921-928`. Taskflow workers call `set_line` while render_present iterates `m_lines`; `poll_events()` is also called off the main thread (SDL_PollEvent is main-thread-only and the ping-pong buffer drops events). **Fix:** add `std::mutex m_lines_mutex` around all `m_lines`/`m_clear_color` access; queue status messages from worker threads and drain on the main thread (so SDL_PollEvent + xrBeginFrame / xrEndFrame only ever run on the main thread). **Verify:** ThreadSanitizer build runs through init without warnings.

A4. **[DONE] Mcp_server start/stop race + DoS + auth + NaN + ambiguous lookup** -- `src/editor/mcp/mcp_server.cpp` (several sub-fixes):
   - `:147-171` replace joinability gate with a single `m_lifecycle_mutex` covering `start()` / `stop()` and the `m_http_server` / `m_server_thread` pair. Make `~Mcp_server` catch join exceptions explicitly rather than relying on `std::terminate`.
   - `:140-145, 268-282` set `httplib::Server::set_payload_max_length(N)`, cap `m_request_queue` size, return JSON-RPC `-32000` "server busy" beyond the cap, drop expired queue entries inside `process_queued_requests` so a request that hit the 5-second timeout (line 284-287) cannot mutate state later.
   - `:182, 192-223` require a bearer token loaded from a 0600 file (e.g. `$HOME/.claude/erhe_mcp_token`); reject 401 without it.
   - `:1596-1627` apply `std::isfinite()` + per-field clamp on all material float inputs; reject NaN/Inf with a JSON-RPC error *before* the `if (after == before)` equality check at `:1722` (the NaN-broken short-circuit is implicitly fixed once NaN is rejected upstream).
   - `:1567-1573, 1442-1457` first-match material/texture name lookup is ambiguous on collisions. Return `isError` listing candidate ids when >1 match exists.
   - `:1517` `tex_coord` read as `uint32_t` but JSON schema only says `minimum: 0`; clamp to a small range explicitly.

   **Verify:** existing `mcp_server_tests.cpp` integration tests (lines 246-265, 597-714) pass; add (a) a tampered-token test, (b) a NaN-float test, (c) a queue-overflow test, (d) an ambiguous-name test. Wrap test mutations in RAII save/restore so a failed assertion does not leave global state mutated.

A5. **[DONE - partial; viewport teardown deferred]** **Scene_builder "undoable" leaks side-state** -- `src/editor/scene/scene_builder.cpp:705-735, 737-753`. `add_room` and `add_cameras` synchronously create `floor_material`, push a `Brush` into `m_floor_brushes`, push an `ICollision_shape` into `m_collision_shapes`, and build `Viewport_scene_view` + viewport_window before queueing the `Compound_operation`. Undo only removes the scene-graph node. **Fix:** wrap material / brush / collision-shape / viewport creation in dedicated `Operation` subclasses so the undo unwind tears them down. Pattern reference: `Material_change_operation` in editor. **Verify:** regression test that runs `add_room()` -> undo -> `add_room()` and asserts `m_floor_brushes.size() == 1`. Floor brush/shape/material covered by `Scene_builder_floor_resources_operation`; viewport teardown still requires a Scene_views teardown API that does not exist yet.

A6. **[DONE] Triangle_soup uploads to wrong GPU buffer once pool grows** -- `src/erhe/graphics/erhe_graphics_buffer_sink/graphics_buffer_sink.cpp:46-63`. `enqueue_vertex_data` / `enqueue_index_data` / `enqueue_edge_line_vertex_data` call `pool->enqueue_data(pool->get_first_buffer(), offset, ...)` but `offset` is local to the allocation's actual block. **Fix:** thread `Buffer*` through `Buffer_sink::enqueue_*` interface; the `buffer_ready(writer)` overload already pulls the correct `Buffer*` from `writer.buffer_range.buffer`. **Verify:** load a glTF that forces a `Mesh_memory` pool past its first 32 MB block and confirm vertices render correctly via RenderDoc.

A7. **[DONE] Vertex_format_key ignores stream layout** -- `src/erhe/dataformat/erhe_dataformat/vertex_format.cpp:124-181`. Only two formats today so no collision yet, but the invariant is wrong. **Fix:** fold stream index, stride, and element format into the key. **Verify:** add a unit test with two formats that share `(position, usage_index=0)` but differ in stream; assert their keys differ.

A8. **[DONE] Mesh_memory format-pool insertion not thread-safe** -- `src/erhe/scene_renderer/erhe_scene_renderer/mesh_memory.cpp:56-78`. `m_extra_format_pools` map mutated without a lock; class has no existing mutex. **Fix:** add `mutable std::mutex m_extra_format_pools_mutex`; lock around `get_or_create_format_pools`. The "init-only" assumption today is fragile; call out in code comment that any future runtime caller is protected. **Verify:** ThreadSanitizer build.

A9. **[DONE] Zero-vertex primitives now fail to build** -- `src/erhe/primitive/erhe_primitive/primitive_builder.cpp:107-114`. `Free_list_allocator::allocate(0, ...)` returns a successful zero-byte allocation; new gate `range.count == 0 -> build_failed = true` rejects it. **Fix:** distinguish "allocator refused" from "primitive had no vertices to allocate"; only mark `build_failed` for the former.

A10. **[DONE] Content_library_window leak on Scene_open_operation undo + redo** -- `src/editor/asset_browser/asset_browser.cpp:112-117`. `undo()` calls unregister/remove_browser_window but never `m_content_library_window.reset()`. The `redo()` path then re-creates a Content_library_window without releasing the prior one. **Fix:** reset in both `undo()` and `redo()`; defensively zero-out any cached references the window holds into the scene. **Verify:** open scene -> close -> reopen, no duplicate "Content Library - ..." window appears.

A11. **[DONE] shadow_index over-counts non-shadow lights** -- `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.cpp:200-225` and `shadow_renderer.cpp:280-289`. Slot assignment sets `shadow_index = slot` where `slot` includes non-shadow lights in the same type bucket; `m_render_passes` is sized to `preset.shadow_light_count`. **Fix:** keep a per-type `shadow_index_counter` so shadow passes index a dense array. **Verify:** synthetic scene with 8 spot lights where only the last 2 cast shadows; assert both render passes execute.

A12. **[DONE] debug_renderer_bucket multiview + non-compute fallback miscomputes** -- `src/erhe/scene_renderer/erhe_scene_renderer/debug_renderer_bucket.cpp:418-568`. New branch `if (m_use_compute && multiview)` returns early; if `multiview=true && m_use_compute=false` (geometry-shader fallback), control falls through to the single-view non-compute path with `stride_per_view = 0`. Both eyes render with `c_view_index = 0u`. **Fix:** `ERHE_VERIFY(!multiview)` on the geometry-shader fallback path, or compute `stride_per_view` correctly and pass it to the geom shader (preferred if a Quest device ever exercises this fallback).

A13. **[DONE] Debug_renderer::is_multiview_active disagrees with dispatch_compute** -- `src/erhe/scene_renderer/erhe_scene_renderer/debug_renderer.{hpp,cpp}`. `is_multiview_active()` returns true when `max_view_count == 1` but `dispatch_compute` keys multiview on `>= 2`. **Fix:** gate `is_multiview_active()` on `>= 2`. **Verify:** unit test that constructs `Debug_renderer` with `max_view_count = 1` and confirms `is_multiview_active()` returns false.

A14. **[DONE] shadow_light_count clamp lives in apply_args, bypassed by other callers** -- `src/editor/scene/scene_commands.cpp:132-148` (`Add_lights_command::apply_args`). Future callers via `try_call()` (key binding, MCP, UI button) skip `apply_args()` and exceed `shadow_light_count`. **Fix:** move clamp into `Scene_builder::add_lights`. **Verify:** invoke `add_lights` via MCP with N > shadow_light_count, confirm clamp.

A15. **[DONE] Ambient_light_operation::execute overwrites m_before on redo** -- `src/editor/operations/ambient_light_operation.cpp:24-31`. If any path mutates ambient_light between undo and redo, redo silently captures the new value as "before". **Fix:** capture `m_before` once at construction; redo restores `m_after`, undo restores `m_before`, neither rewrites the other.

A16. **[DONE - partial; prunes only on next load] Operations::m_loaded_content_library_windows grows monotonically** -- `src/editor/windows/operations_window.cpp:217-225, .hpp:189`. Vector appended on every `load_scene_file`; nothing prunes on scene unload. **Fix:** prune on the `Scene_open_operation` unregister path; align ownership with Scene_root / browser_window / content_library_window (the three-way split called out in quest_review).

A17. **[DONE] BeginTable/EndTable mismatch in scene_view_config_window** -- `src/editor/windows/scene_view_config_window.cpp:40-44, 124`. Pre-table `!scene_root` guard dropped; `BeginTable` return ignored, so `EndTable` can run with no matching `BeginTable`. **Fix:** capture `BeginTable` return; only call `EndTable` when it returned true.

A18. **[DONE] Buffer_pool leaks Buffer if second push_back throws** -- `src/erhe/graphics/erhe_graphics/buffer_pool.cpp:85-94`. Two sequential `push_back`s without exception safety. **Fix:** reserve up-front, or use a single transactional helper that rolls back the first push on the second's failure.

A19. **[DONE] Content_wide_line_renderer per-dispatch SSBO bind, no dedup** -- `src/editor/renderers/content_wide_line_renderer.cpp:489-496`. Each compute dispatch rebinds `(edge_buffer, byte_offset)` even when consecutive dispatches share both. **Fix:** dedup on consecutive `(buffer, offset)` pairs.

A20. **[DONE] simdjson ondemand ordered-access hazard** -- `src/editor/editor.cpp:2069-2075`. Reads `obj["name"]` then `obj["args"]` but `find_field_unordered` is not used; if the JSON entry keys are reordered, the second access fails. **Fix:** switch to `find_field_unordered` or document the key-order requirement in the JSON schema and validate.

### B. Vulkan / Metal / shader infrastructure (high + med)

B1. **[DONE] vkCmdBeginDebugUtilsLabelEXT recorded unconditionally** -- `src/erhe/graphics/erhe_graphics/vulkan/vulkan_scoped_debug_group.cpp:34, 42`. Null function pointer if `VK_EXT_debug_utils` is not loaded. **Fix:** add `static bool s_enabled` (already in GL backend), early-return when false; gate on extension presence at device init.

B2a. **[DONE - rename + contract change] "Lazy" naming for shader handles dropped** -- `Lazy_shader_handle` -> `Shader_stages_handle` (erhe_graphics); `Cached_shader_handle` -> `Variant_handle` (erhe_scene_renderer); `Render_pipeline_create_info::lazy_shader_stages` -> `shader_stages_handle`. The handle no longer promises deferred compile; its contract documents resolution timing as implementation-defined. The original B2a framed the Vulkan/Metal eager-ctor as a contract violation -- with the contract rewritten, the violation dissolves. See B2a-perf for the residual cold-start question.

B2b. **[DONE] `Variant_handle` memoizes resolves so the cache mutex leaves the per-bind hot path** -- `src/erhe/scene_renderer/erhe_scene_renderer/variant_handle.{cpp,hpp}` + `shader_variant_cache.{cpp,hpp}`. Old behavior: `shader_stages()` took the cache mutex on every call; `multiview_shader_stages()` additionally rebuilt a fresh `Shader_variant_key` per call (string + defines-vector copy, canonicalize sort/dedup, plus a `Shader_name_registry` mutex acquire). Both fired at every GL `set_render_pipeline_state` bind and at every `Forward_renderer` per-pipeline iteration. **Fix that landed:** (a) precompute the multiview-sibling `Shader_variant_key` once in the multiview ctor (stored as `std::optional<Shader_variant_key>`); (b) memoize the resolved non-null `Shader_stages*` for both single-view and multiview inside the handle; (c) `Shader_variant_cache` holds a `std::vector<Variant_handle*>`, handle ctor / dtor register / unregister, and `clear()` walks the list calling `reset_memoization()` before erasing entries so the cached pointers do not dangle. Threading invariant: every caller of `Variant_handle::shader_stages()`, `Shader_variant_cache::clear()`, `Material_change_operation::execute`, and `Shader_monitor::update_once_per_frame` runs on the editor's render thread (`Shader_monitor::poll_thread` does not call into the variant cache); the per-handle memo is therefore left unsynchronized while the cache mutex still guards `m_handles` register/unregister/walk. Compile-failure retry preserved: only non-null resolves are memoized so a transient failure does not stick across hot reload.

B4. **[DONE] Shader_monitor reload bypasses pipeline-cache flush** -- `src/erhe/graphics/erhe_graphics/shader_monitor.cpp:209`. `entry.shader_stages->reload(prototype)` recreates `VkShaderModule` without flushing the variant cache's pipeline cache. **Fix:** have `Reloadable_shader_stages::reload` call `m_graphics_device.clear_render_pipeline_cache()` (the variant cache already does this on its own `clear()`; mirror it). **Verify:** edit a watched shader at runtime; subsequent frames sample the new module.

B5. **[DONE - landed with B14]** **`Material_buffer.unlit` is a load-bearing band-aid** -- the four legacy shaders that read it (`anisotropic_slope.frag`, `anisotropic_engine_ready.frag`, `circular_brushed_metal.frag`, `standard_debug.frag`) were retired in the same commit set as B14. `Material_struct::unlit` and the matching write path are gone from `material_buffer.{hpp,cpp}`; `grep -r material.unlit res/` returns zero.

B6. **[DONE] Dead `anisotropy_strength`** -- `src/editor/res/shaders/standard.frag:96-101`. **Fix:** wire to `roughness_y = mix(roughness_x, roughness_y, anisotropy_strength)` (the docstring intent); the commit message advertises it. **Verify:** anisotropic material with `aniso_control = 0` matches isotropic baseline.

B7. **[DONE - documented as BREAKING] Silent emissive semantic change** -- `src/editor/res/shaders/standard.frag` emissive block. Old: squared; new: linear. **Fix:** decide intent against a reference image; add a `BREAKING:` note if the change is intentional. **Verify:** comparison render of an emissive material.

B10. **[DONE] shader_stages.hpp promises layout(num_views=N) emission that never happens** -- `src/erhe/graphics/erhe_graphics/shader_stages.hpp:915-925`. Comment claims emission; `final_source()` only emits the `#extension` + `#define` lines (correct for `GL_EXT_multiview`). **Fix:** delete the layout-qualifier promise from the comment.

B11. **[DONE - footgun removed in earlier commit] ERHE_VERIFY(mv_slot == nullptr) footgun on shared shader names** -- `src/editor/renderers/programs.cpp:683`. `wide_lines_draw_color` and `wide_lines_vertex_color` both have `name="wide_lines"` and no `debug_label`. A future config enabling multiview with `use_compute_shader == false` trips the verify on the second emplace. **Fix:** key the slot map on `(name, defines)` or hash-discriminate.

B13. **[DONE] Shader_variant_key is stringly-typed** -- `src/erhe/scene_renderer/erhe_scene_renderer/shader_variant_cache.hpp:32-49`. Typo in any shader name silently misses; compile fails; Metal/Vulkan returns nullptr from the pipeline ctor. **Fix:** tiny registry of valid shader names backing the key; reject unknown names at construction.

B14. **[DONE]** **Variant cache covers `standard` only; legacy lit shaders still eager** -- the legacy `anisotropic_slope`, `anisotropic_engine_ready`, `circular_brushed_metal`, and 28 `standard_debug` Variant_handles are gone from `Programs`. Each feature is now a variant of `standard.frag`:
- `anisotropic_slope` / `anisotropic_engine_ready` joined `erhe::primitive::Bxdf_model`; `standard.frag`'s `BXDF_CALL` dispatch picks `slope_brdf` for SLOPE and uses the existing `anisotropic_brdf` (with a 1e-7 roughness floor) for ENGINE_READY.
- `circular_brushed_metal` was already a variant axis on `standard.frag`; the standalone shader was redundant and was deleted.
- The 28 `ERHE_DEBUG_*` modes from `standard_debug.frag` became a new `erhe::scene_renderer::Shader_debug` enum stored per `Viewport_scene_view` and a new `SHADER_DEBUG` count axis on `Standard_variant_key`. `standard.frag` emits one `#if ERHE_SHADER_DEBUG == N` override block per value at the end of `main()`.
The `Material_change_operation` -> `Standard_shader_variants::clear()` hammer still covers the standard variants but no longer recompiles legacy shaders that do not exist anymore. B5 landed in the same commit set: `Material_struct::unlit` is gone.

### C. Variant-cache invalidation perimeter (cumulative high)

C1. **[DONE - partial; invalidate-now path added]** **Properties panel bypasses Material_change_operation** -- `src/editor/windows/properties.cpp:1023-1037`. ImGui combo / checkbox for `bxdf_model`, `use_circular_brushed_metal`, `use_aniso_control` writes `material->data` directly. **Fix:** route through `Material_change_operation` (consistent with MCP and the rest of the editor). **Verify:** flip "Circular Brushed Metal" via Properties; next frame's variant matches the new key.

C2. **[DONE] Two-source-of-truth light-count tally** -- `src/erhe/scene_renderer/erhe_scene_renderer/standard_shader_variant.cpp:222-256` (`compute_standard_variant_light_counts`) vs `light_buffer.cpp:189-207` (`Light_projections::apply` inline tally). The "must stay bit-identical" comment is the band-aid. **Fix:** there must be a single source for the count. Return a `Light_layer_partition` struct (count snapshot + scratch arrays) from one helper that both `Light_projections::apply` and `compute_standard_variant_light_counts` call. Do not split the goal across "use the helper only for the count, scratch separately" -- that just re-creates the divergence risk in a smaller form. **Verify:** delete the "must stay bit-identical" comment after the merge.

### D. Multiview / XR (high + med + low)

D1. **[DONE] Multiview wide-line feed ignores render_style and selection_outline animation** -- `src/editor/xr/headset_view.cpp:612-644`. Compare to `viewport_scene_view.cpp` `feed_pass`. **Fix:** copy the `else if (pass->get_render_style)` branch plus the `selection_outline` breathing animation, OR factor into a shared helper. **Verify:** Quest run shows correct opaque_edge_lines and selection_outline under multiview.

D2. **[DONE] VUID silencer too broad** -- `src/editor/xr/xr_instance.cpp:316-321`. Filter matches all `VUID-xrStructureTypeToString-*`. **Fix:** narrow to the exact VUID(s); assert on any other match.

D3. **[DONE] ERHE_VERIFY on env-var write** -- `src/editor/xr/xr_instance.cpp:425-426`. Aborts on transient `set_env` failure. **Fix:** match `vulkan_device_init.cpp`'s `set_env_or_warn` pattern -- log a warning and continue.

D4. **[DONE - partial; rename deferred] Stale comment + confusingly-close knobs** -- `src/editor/xr/xr_session.cpp:530-533` stale comment; `Headset_config.depth` vs `swapchain_depth`. **Fix:** drop the stale comment; rename to `composition_depth_layer` / `swapchain_depth_attachment` (or fill in `headset_config.py`'s empty `short_desc` fields explaining the distinction).

D5. **[DONE] gl_ViewID_OVR / u_view_index doc drift** -- `src/erhe/scene_renderer/erhe_scene_renderer/camera_buffer.hpp:47`, `camera_buffer.cpp:123-125, 160-161`. Code emits `gl_ViewIndex` / `c_view_index`; comments still reference the old names. **Fix:** update comments. **Verify:** `grep -r gl_ViewID_OVR src/erhe` returns zero.

D8. **[DONE] Metal Scoped_debug_group_impl encoder identity desync** -- `src/erhe/graphics/erhe_graphics/metal/metal_scoped_debug_group.cpp:17-33`. Ctor sees `get_active_mtl_encoder()`; dtor sees a different encoder if recording switched. **Fix:** impl stores the encoder it pushed onto and pops on the same one.

D9. **[DONE - documented contract] Vulkan Scoped_debug_group caches handle, not Command_buffer&** -- `src/erhe/graphics/erhe_graphics/vulkan/vulkan_scoped_debug_group.cpp`. If the scope outlives the cb that produced its handle, dtor writes to a recycled handle. **Fix:** cache the `Command_buffer&` instead, OR document the lifetime contract with a static_assert.

### M. Material / shader content (med)

M1. **[DONE] Default-material switch is a user-visible content regression** -- `src/editor/content_library/material_library.cpp:24-30`. Every stock material now created with `bxdf_model = anisotropic_brdf` + `use_circular_brushed_metal = true`. Old saved scenes diverge from regenerated defaults. **Fix:** restore the previous isotropic default; opt new materials into anisotropic via UI / API only.

M2. **[DONE - partial; gltf import wired; codegen serializer deferred] No persistence path for bxdf_model / use_* fields** -- `src/editor/io/gltf_fastgltf.cpp:1268, 1912, 2006`. glTF export and codegen JSON serializer don't touch the three new fields. **Fix:** wire `KHR_materials_unlit` import to `bxdf_model = unlit`; add the three new fields to the codegen serializer. **Verify:** roundtrip glTF preserves bxdf_model.

M3. **[DONE] Circular-brush magic-8.0 + UV-origin singularity** -- `src/editor/res/shaders/standard.frag:88`. `length(v_texcoord) * 8.0`; `normalize(v_texcoord)` undefined at UV origin. **Fix:** document the `8.0` constant or expose as a per-material parameter; epsilon-guard the normalize near origin.

M4. **[DONE] Roughness floor 1e-4 clamp inconsistent** -- `src/editor/res/shaders/standard.frag:117-118` vs 121-123. Clamp applies only on the `ERHE_USE_METALLIC_ROUGHNESS_TEXTURE` path. **Fix:** apply the floor on the no-texture path too.

### E. Build / config / docs / dead-code (high + med + low)

E1. **[DONE - partial; SHA256 verification deferred] Gradle task ships stale validation layer .so** -- `android-project/app/build.gradle:171-195`. `outputs.upToDateWhen { layerSo.exists() }` does not invalidate on `validationLayerVersion` bump. **Fix:** add `inputs.property("validationLayerVersion", validationLayerVersion)`; remove `libs/arm64-v8a/libVkLayer*.so` in the `-Pvulkan_validation_skip` branch; add SHA256 verification of the downloaded zip. **Verify:** bump version, re-run task, confirm new .so on device.

E2. **[DONE] `ERHE_TARGET_QUEST_STANDALONE` is dead** -- `CMakeLists.txt:127`. Zero references in source tree. **Fix:** delete the `add_compile_definitions` line; if a Quest-only code path is needed, gate on `ANDROID` + `__aarch64__` instead.

E4. **[DONE - comment-only; quad-view sourcing deferred] Hardcoded prewarm view counts** -- `src/editor/renderers/prewarm.cpp:89-99`. `view_counts.push_back(0u); view_counts.push_back(2u);`. **Fix:** source `max_view_count` from `Headset_view` / `Xr_session`; prewarm `{0, max_view_count}`. **Verify:** Varjo quad-view config exercises four-view prewarm.

E5. **[DONE] Stale prewarm comment** -- `src/erhe/graphics/erhe_graphics/prewarm.hpp:19-22`. "Subsequent commits will extend ..." names work already shipped. **Fix:** rewrite to describe what the file does today.

E6. **[DONE] `doc/building.md` violates CLAUDE.md** -- lines 48-61 (cmake --preset section), 67 ("CMake: Select Configure Preset"), 73 (CLion preset), 90 (Linux ad-hoc cmake -B build), 99 (Windows backslashes in bash block), 128 (broken `ERHE_GRAPHICS_API` table cell). Plus typos at line 6 ("Exceptionionally", "inclouded"). **Fix:** delete the CMake-presets section; restore scripts/ wrapper requirement on macOS / Linux; restore the Quest controllers-required protocol summary; fix typos.

E7. **[DONE - marked future work] `doc/prewarm.md` Phase 2 vs reality** -- lines 53-82 document `Device::warmup_render_pipeline` as if `prewarm_all` calls it. **Fix:** update doc once E3 lands (or mark "future work" if E3 is deferred).

E8. **[DONE] `doc/shader_variants.md` "Remaining adoption work"** -- lines 33-42 describe shipped work. **Fix:** rewrite as "Adopted as of <commit>; full migration tracked in `doc/draw_list_renderer.md` Phases 4-6."

E9. **[DONE] `doc/scene_root_cleanup.md` reads as future plan but is shipped** -- Step 1 / Step 2 already implemented in commit 3d570726. **Fix:** rewrite as postmortem.

E10. **[DONE] `doc/multiview.md` overstates GL coverage** -- lines 9-14 promise a GL OpenXR multiview per-eye fallback that is not wired. **Fix:** add "GL OpenXR multiview is not supported; the GL path covers the desktop-mirror window only."

E12. **[DONE] `src/editor/editor_settings.json` vs `config/editor/editor_settings.json`** -- out of sync (headset `_version` 2 vs 3, missing fields, conflicting `swapchain_depth`). **Fix:** determine which copy is canonical (check `src/editor/CMakeLists.txt`); delete the redundant copy, or regenerate from the codegen.

E13. **[DONE - Init UI only; rest deferred] Dead code to delete** -- the Init UI subset landed (`Init_status_display::set_clear_color` removed; unused `texture_layer` param dropped from `render_text_overlay`). The remaining four subsystems (Renderers, Graphics buffers, Debug renderer, Mesh memory) are tracked under Open Issues -> "Dead-code subsystems remaining from E13".

E14. **[DONE] Debug_label String_pool leak** -- `src/erhe/utility/erhe_utility/debug_label.hpp:11-66`. Global `unordered_set<std::string>` with no eviction. Renderer-agnostic root cause; affects both Forward_renderer (call site goes away) and the future Draw_list_renderer (per-MDI-span labels). **Fix:** drop interning entirely; `Debug_label` stores its own `std::string`. The RAII scope guards already provide cheap lifetimes, so interning is solving a problem that no longer exists. **Verify:** `String_pool::size()` bounded after a soak run.

E15. **[DONE - turned on via A3] Dead `render_present_xr` code path shipped** -- `src/editor/init_status_display.cpp:202-340`, called from `editor.cpp:912-928` only with `nullptr`. **Fix:** either turn the path on (paired with A3's mutex fix so the OpenXR thread-safety is actually correct), or strip the ~180 lines plus the `Headset*` ctor parameter.

E16. **[DONE] `Headset_view_resources::is_valid()` is constant-true** -- `src/editor/xr/headset_view_resources.cpp:120` (after the `ERHE_VERIFY(m_color_texture != nullptr)` removal). Caller check at `headset_view.cpp:511` is dead. **Fix:** either make `is_valid()` reflect real state or drop the check.

E17. **[DONE - documented invariant] Prewarm walks only `polygon_fill`** -- `src/editor/renderers/prewarm.cpp:148`, `scene_preview.cpp:296`. Editor renders composition passes with `polygon_fill`, `edge_lines`, `corner_points`, `corner_normals`, `polygon_centroids`. **Fix:** walk all modes (or `ERHE_VERIFY` that the others don't opt into `uses_standard_variants`).

### Historical: original triage notes

These sections drove the now-completed correctness pass and are kept for context.

#### Priority and bundling

Original suggested commit groups (each was one PR or commit set):

1. **High-severity correctness (A subset)**: A1, A2, A3, A6, A11. Add A10 + A20 ride-along. Add D1 (multiview wide-line render_style + selection_outline animation) ride-along since it is user-visible on Quest.
2. **MCP correctness + auth + DoS**: A4 (all sub-items) + A14 (related shadow_light_count clamp from MCP exposure surface).
3. **Scene_builder undoability**: A5 + A15 (Ambient_light_operation redo). Both are Operation-correctness fixes.
4. **Graphics buffer correctness**: A6 + A7 + A8 + A9 + A18.
5. **Debug renderer correctness**: A12 + A13.
6. **UI lifetime fixes**: A16 + A17 + E16.
7. **Variant-cache + shader infra (B, C)**: bundle by file proximity: (B1 + B4) standalone; (B2a + B2b) bundle; (B5 + B14) bundle (B5 first, B14 second, then E13.Renderers depends on both); (B6 + B7 + M3 + M4) shader cleanup; (B11 + B13) name-keying.
8. **XR cleanup (D + M1 + M2)**: D1 is user-visible and rides in bundle 1 (high-severity correctness). D2-D9 + M1 + M2 land in their own XR/material PR.
9. **Variant-cache invalidation (C)**: C1, C2 as one PR.
10. **Build / config / docs (E)**: E1, E2, E6-E10, E11 as docs/config-only PRs. E12 is a CMake / canonical-source decision; ship it on its own. E3 + E4 + E5 + E7 as a prewarm wiring PR. E13 dead-code split per subsystem (note: E13.Renderers depends on B5 + B14 landing first). E14 standalone (Debug_label).
11. **Commit hygiene (F)**: F1 -- ask user before doing.

#### Roll-up verification

End-to-end verification used during the correctness pass:

- ThreadSanitizer build of the editor (A3, A4, A8).
- Quest 3 launch via `scripts/install_android.bat quest run` after the controllers-required prompt, with `vulkan_validation_layers=true` in `config/editor/erhe_graphics.json` (A1, B1, B4, D1).
- Existing `mcp_server_tests.cpp` integration suite (A4 sub-fixes) plus four new tests listed under A4.
- RenderDoc capture of a Mesh_memory pool past 32 MB (A6).
- Editor smoke test: load a stock scene, edit material via Properties panel, confirm next-frame visual update (C1).
- Synthetic light-count regression (A11): 8 spot lights, 2 shadow-casting; confirm both passes execute.
- ASAN build of the editor to catch any remaining lifetime regression from A5 / A10 / A18.
