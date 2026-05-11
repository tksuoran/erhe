# Quest branch review

Generated 2026-05-11. Reviews the 15 commits that the quest branch carries over main (squashed from 112 original commits, four self-cancelling reverts dropped).

Each commit was reviewed independently by a critical agent looking for bugs, race conditions, lifetime issues, API misuse, std140/std430 layout violations, missed error paths, ASCII / naming / style violations, dead code, and half-finished implementations. After the 15 per-commit reviews, a 16th agent reviewed the cumulative diff (as if all 15 were squashed) for inconsistencies, missed opportunities, and bad architectural choices.

Severity guide: **high** = real bug or correctness / safety issue; **med** = bad API choice, lifetime risk, or maintainability footgun; **low** = style, doc, naming.

## Commits under review

1. `87eb2863` graphics + scene_renderer + xr: Vulkan multiview rendering foundation
2. `380eb642` editor + shaders: end-to-end stereo multiview on Quest 3
3. `a22a90bb` android + graphics: Vulkan validation layer in the APK + glslang 16.3.0
4. `d7aa7f3d` build + xr: ERHE_GRAPHICS_API rename + Headset_config consolidation + OpenXR-SDK 1.1.59
5. `214f16b8` editor + renderer: Init_status_display via OpenXR + Debug_renderer multiview
6. `8f90a2b0` editor: startup command scripts + scene-builder undoable + multiview polish
7. `9feb3c1a` erhe + editor: mesh_memory multi-format pool registry + lazy buffer growth
8. `3d570726` editor + docs: slim Scene_root, viewport fixes, draw list renderer plan
9. `32ecac80` scene_renderer + editor: Standard_shader_variants cache + bucket plumbing
10. `f8dab5f8` graphics + rendergraph + scene_renderer: cb-targeted Scoped_debug_group for RenderDoc
11. `7704e198` scene_renderer + editor: Standard_shader_variants refinement + Lazy_shader_handle
12. `04dc43ba` editor: expose material editing via MCP server + integration tests
13. `c531aac0` editor + graphics: BxDF model + anisotropic brushed-metal materials
14. `407c7be3` editor + scene_renderer + graphics: pipeline prewarm at init
15. `82e4b778` editor: startup script seeds Johnson solids + doc refresh

## Per-commit findings

### Commit 87eb2863 -- graphics + scene_renderer + xr: Vulkan multiview rendering foundation

**TLDR:** Solid groundwork -- Vulkan multiview device-feature query/enable, layered XR swapchain, view-mask plumbing through Render_pass, cameras[N] UBO, and per-shader-stage prelude all hang together. The Headset_view path is admittedly a clear-only stub so most rendering machinery is unexercised. Two latent correctness issues stand out around the single-camera `Camera_buffer::update()` path now uploading a wider block.

**Findings:**

- **[high]** Single-camera `update()` uploads uninitialized bytes when `max_view_count >= 2` -- `src/erhe/scene_renderer/erhe_scene_renderer/camera_buffer.cpp:122-140`. With multiview on, `block_size = entry_size * max_view_count`, but `write_camera_entry()` only writes the first entry. Comment claims entries 1..N-1 are "left zero-initialised"; in fact ring-buffer-acquired slices are not zero-initialized. Single-view callers (`cube_renderer.cpp:115`, `texel_renderer.cpp:69`) running while `max_view_count >= 2` upload garbage in `cameras[1]`. Fix: `std::memset` the trailing bytes or shrink `block_size` to `entry_size` on the single-view code path.
- **[med]** Doc drift -- `gl_ViewID_OVR` / `u_view_index` referenced in C++ comments; code emits `gl_ViewIndex` / `c_view_index` (`src/erhe/scene_renderer/erhe_scene_renderer/camera_buffer.hpp:47`, `camera_buffer.cpp:123-125,160-161`). The macro defined by `res/shaders/erhe_camera_view.glsl` is `c_view_index`. Confuses grep and readers.
- **[med]** `shader_stages.hpp` comment promises a `layout(num_views = N) in;` emission that never happens (`src/erhe/graphics/erhe_graphics/shader_stages.hpp:915-925`). `final_source()` only emits the `#extension` + `#define` lines (correct for `GL_EXT_multiview`). Remove the layout-qualifier promise.
- **[med]** `ERHE_VERIFY(mv_slot == nullptr)` is a footgun on shaders with shared `name` -- `src/editor/renderers/programs.cpp:683`. `wide_lines_draw_color` and `wide_lines_vertex_color` both have `name="wide_lines"` and no `debug_label`. If a future config enables multiview with `use_compute_shader == false`, the second emplace trips the verify. Use `(name, defines)` or hash-discriminate.
- **[med]** Dead `if (m_instance.extensions.FB_passthrough)` comment block in `xr_session.cpp:636-644` -- contains a comment only, no code. Remove or move next to the actual passthrough block.
- **[low]** `Render_command_encoder encoder` constructed and discarded as scaffolding in `headset_view.cpp:464-468`. Comment instead of `(void)encoder;` would be clearer.
- **[low]** `query_multiview_properties` logs unconditionally even when multiview isn't advertised (`vulkan_device_init.cpp:618-619`). Gate on `query_multiview_features.multiview == VK_TRUE`.
- **[low]** Indentation drift in the per-eye block of `xr_session.cpp:655-729` (the new outer `!m_use_multiview` guard re-indented surroundings but the loop body kept old indent).

**No-issues note:** Vulkan multiview chain wiring (features2 pNext, layerCount/viewMask, framebuffer.layers=1, image-view layerCount sweep) is correct. Shared layered XrSwapchain + per-view `imageArrayIndex` is the canonical OpenXR multiview pattern. Debug-line `width^2 = (2 * w0/1)^2 * 0.25` math is consistent with the fragment shader.

### Commit 380eb642 -- editor + shaders: end-to-end stereo multiview on Quest 3

**TLDR:** Large, mostly-coherent multiview wiring. Per-view-strided line compute lines up with the multiview vertex shader. Fragile parts: `Init_status_display`'s new XR path is shipped disabled, a defensive check is dead, and several const-correctness / lifetime smells should be cleaned up before more code piles on top.

**Findings:**

- **[med]** Dead `render_present_xr` shipped, never exercised -- `src/editor/init_status_display.cpp:202-340`, `src/editor/editor.cpp:912-928`. Adds ~180 lines and a `Headset*` ctor parameter, but the only call site passes `nullptr` ("OpenXR mode... intentionally disabled here"). Gate behind `#if 0` or strip.
- **[med]** `Init_status_display::set_line` / `m_lines` is racy under parallel init (`init_status_display.cpp:57-63` writers; lines 120 / 249 readers). The header comment even acknowledges "concurrent set_line() calls... serialized only by the lack of contention in practice." Per CLAUDE.md "No band-aid fixes": fix it, don't document it away.
- **[med]** `Headset_view_resources::is_valid()` is now constant-true after the `ERHE_VERIFY(m_color_texture != nullptr)` removal (`headset_view_resources.cpp:120`). Caller check in `headset_view.cpp:511` is dead. Either drop the check or have `is_valid()` reflect real state.
- **[low]** `const_cast<Render_views_frame&>(frame_in)` in `headset_view.cpp:499` -- callback signature lies about const-ness (`view_resources->update()` writes back into the frame).
- **[low]** `const_cast<Shader_stages*>(programs.get_multiview(name))` in `app_rendering.cpp:484`. Either make `Render_pipeline_create_info::multiview_shader_stages` `const`, or have `Programs::get_multiview` return non-const.
- **[low]** `doc/multiview.md:357-389` lists "Remaining work" that includes scaffolding wired in this commit (`m_multiview_graphics_bind_group_layout`, the `ERHE_MULTIVIEW` branch in `line_after_compute.vert`) with no caller -- partially-multiview surface area waiting for the next commit.
- **[low]** Encoder constructed before `Scoped_render_pass` in `headset_view.cpp:557-558` and `init_status_display.cpp:260-261`. Works today; risky if the encoder dtor ever touches the closed pass. Construct inside the scoped block.
- **[low]** Single-view padding-copy in `content_wide_line_renderer.cpp:426-428` populates `cameras[1..N-1]` with `cameras[0]` even though the compute shader reads `view.view_count`. Trust `view_count` or trust `max_view_count`; pick one.

**No-issues note:** Per-view stride math (`stride_per_view = padded_edge_count * 6`, vertex-shader `idx = gl_VertexID + gl_ViewIndex * stride_per_view`) is internally consistent. std140 layout of `cameras[N]` + `world_from_node` is a multiple of 16 at every transition. Compute->vertex barrier uses `shader_storage_barrier_bit`. No transform-feedback / geometry-shader interactions.

### Commit a22a90bb -- android + graphics: Vulkan validation layer in the APK + glslang 16.3.0

**TLDR:** Vulkan device-create pNext fix and the glslang patch are sound, but the gradle task is fragile (no checksum, version bump does not reinvalidate cached .so) and the multiview wide-line feed in `headset_view.cpp` silently mis-renders edge-line / outline passes because it lacks the `get_render_style` + animated-outline logic from `viewport_scene_view.cpp`.

**Findings:**

- **[high]** Version bump can ship the OLD validation layer -- `android-project/app/build.gradle:171-195`. `fetchVulkanValidationLayer` declares `outputs.file(layerSo)` whose path is invariant across versions, and `outputs.upToDateWhen { layerSo.exists() }`. Bumping `validationLayerVersion` does not invalidate the task. Fix: declare `inputs.property("validationLayerVersion", ...)` or write the version into a marker file next to the .so.
- **[high]** Multiview wide-line feed in `headset_view.cpp:612-644` does NOT honor `Composition_pass::get_render_style`. `viewport_scene_view.cpp` has the `else if (pass->get_render_style)` branch plus the `selection_outline` breathing animation; the new headset code has neither. `opaque_edge_lines_*` and `selection_outline` render with default `Primitive_interface_settings{}` (white, default width) in VR. Fix: copy the full `feed_pass` body + animation block (or factor it into a shared helper).
- **[med]** `-Pvulkan_validation_skip` does not actually opt out once the .so is staged. `gradle clean` only clears `build/`, not `libs/`. Once a build has run without the flag, every subsequent build (with or without the flag) bundles the .so. Stage into `build/` or delete in the `else` branch.
- **[med]** No integrity verification on the downloaded zip (`build.gradle:179`). Plain `ant.get` over HTTPS, no SHA256, no fallback URL.
- **[med]** glslang patch substitutes `DebugInfoNone` for everything that is not `OpConstant`. Correctly handles `OpConstantComposite` / `OpSpecConstantComposite` but also drops `OpSpecConstant` (a scalar spec constant). Spec wording is strict ("OpConstant"), so dropping is safe but lossy; document or relax to `OpConstant || OpSpecConstant`.
- **[low]** Stale `.gitignore:71` comment references `-Pvulkan_validation=1`; the actual property is `-Pvulkan_validation_skip` (default is to bundle).
- **[low]** Shader-source workaround in `visualize_depth.frag` / `standard_debug.frag` (move file-scope const arrays inside functions) is dead code once the glslang patch lands. Either revert or leave a one-line "kept for clarity, patch also handles" comment.
- **[low]** `editor.cpp:702` / `:698` line numbers cited in multiple new doc files will rot the moment editor.cpp is touched. Name the function/lambda instead.
- **[low]** Redundant inner `if (!layerSo.exists())` inside `doLast` after `outputs.upToDateWhen` (`build.gradle:174-175`). Harmless but suggests confusion about Gradle's incremental-build model.

**No-issues note:** `VkPhysicalDeviceVulkan11Features` consolidation correctly subsumes 16-bit storage and multiview. SPIR-V cache salt embedding `GLSLANG_VERSION_*` correctly invalidates pre-bump cache entries.

### Commit d7aa7f3d -- build + xr: ERHE_GRAPHICS_API rename + Headset_config consolidation + OpenXR-SDK 1.1.59

**TLDR:** Solid rename and consolidation, but contains a real bug (`ERHE_GRAPHICS_API_NULL` typo bricks the headless backend's render-pipeline include), defines `ERHE_TARGET_QUEST_STANDALONE` that nothing reads, ships overly broad VUID silencing plus aggressive `ERHE_VERIFY` on env-var writes, and leaves a stale code comment in `xr_session.cpp`.

**Findings:**

- **[high]** `ERHE_GRAPHICS_API_NULL` typo breaks the headless backend -- `src/erhe/graphics/erhe_graphics/render_pipeline.cpp:12`. CMake defines `ERHE_GRAPHICS_API_NONE`; this file uses `#elif defined(ERHE_GRAPHICS_API_NULL)`. When `ERHE_GRAPHICS_API=none` the `null_render_pipeline.hpp` is never included, headless build won't compile. Pre-existing typo carried through the rename. Replace with `_NONE`.
- **[high]** `ERHE_TARGET_QUEST_STANDALONE` defined (`CMakeLists.txt:127`) but referenced nowhere. Dead. Remove or land the call sites in the same commit.
- **[high]** Overly broad VUID silencer (`xr_instance.cpp:316-321`). Filter matches all `VUID-xrStructureTypeToString-*`. Justification covers a single third-party-layer bug; suppression hides future real VUIDs in the same function. Match the exact VUID instead.
- **[med]** `ERHE_VERIFY(erhe::utility::set_env(...))` aborts the editor on transient env-var write failures (`xr_instance.cpp:425-426`). The helper deliberately returns bool for soft-warn callers (`set_env_or_warn` in `vulkan_device_init.cpp`). Match the Vulkan helper's warn-and-continue.
- **[med]** Stale comment in `xr_session.cpp:530-533` claims "On Vulkan the depth swapchain creation is currently disabled" while the very next line replaces the hard-coded `true` with a config-driven value.
- **[med]** Two confusingly-close knobs: `Headset_config.depth` (toggles XR_KHR_composition_layer_depth + Varjo env-depth, `xr_instance.cpp:374`) and `Headset_config.swapchain_depth` (toggles depth swapchain creation, `xr_session.cpp:534`). User toggling one and not the other ends up in a "extension on, no swapchain to submit" failure mode.
- **[med]** `src/editor/editor_settings.json` is out of sync with `config/editor/editor_settings.json` (versions 2 vs 3, missing fields, conflicting `swapchain_depth`).
- **[low]** Encapsulation leak in the codegen split -- `Headset_config.hpp` generated into erhe::xr but `.cpp` compiled into the editor target (`src/editor/CMakeLists.txt:689-697`). Non-editor consumers of erhe::xr will fail to link.
- **[low]** Editor now unconditionally links erhe::xr (`src/editor/CMakeLists.txt:751-755`) even on headless / non-OpenXR builds. Verify `xr_action.cpp` is harmless when `ERHE_XR_LIBRARY_OPENXR` is undefined.
- **[low]** `erhe::utility::set_env` is now a public utility with no thread-safety documentation; on most libcs `setenv` is unsafe against concurrent `getenv`.

**No-issues note:** The rename itself is thorough -- `git grep ERHE_GRAPHICS_LIBRARY` yields zero stragglers. The editor.cpp simplification (passing `m_editor_settings.headset` directly instead of an ad-hoc Xr_configuration aggregate) is a clean win.

### Commit 214f16b8 -- editor + renderer: Init_status_display via OpenXR + Debug_renderer multiview

**TLDR:** Debug_renderer multiview port and Init_status_display XR plumbing land cleanly with reasoned shader / UBO layout, but the commit ships a documented OpenXR-thread-safety race instead of fixing it (band-aid), introduces dead code, and weakens (not strengthens) the Mcp_server stop-race story.

**Findings:**

- **[high]** Documented-but-not-fixed OpenXR thread-safety race -- `editor.cpp:921-928`, `init_status_display.cpp:294-425`. New comment block admits the race ("concurrent set_line() calls... serialized only by the lack of contention"). OpenXR forbids concurrent calls into the same `XrSession`. Today `programs_load_task` calls `set_line` from inside the task lambda while other taskflow nodes may execute concurrently. CLAUDE.md "No band-aid fixes" applies. Add the mutex now, or queue messages and drain on the main thread.
- **[high]** Mcp_server stop-race regressed, not fixed (`mcp/mcp_server.cpp:147-171`). New gate (`if (!m_server_thread.joinable() && !m_http_server) return;`) handles post-listen-pre-destructor case but exposes a new TOCTOU: if `stop()` runs between `m_http_server = make_unique` and `m_server_thread = ...`, old gate (`m_running` atomic) early-returned; new gate proceeds, resets `m_http_server`, then `start()` spawns a thread that dereferences null. Trade-off, not a win. Use a mutex (CLAUDE.md prefers them). Also `~Mcp_server() noexcept { stop(); }` will `std::terminate` if `join()` throws.
- **[med]** Multiview + non-compute fallback silently miscomputes -- `debug_renderer_bucket.cpp:418-568`. The new branch `if (m_use_compute && multiview)` returns early; if `multiview=true` and `m_use_compute=false` (geometry-shader fallback), control falls through to the single-view non-compute path with `stride_per_view = 0`. Geometry shader's `c_view_index` is `0u` for both eyes. Add `ERHE_VERIFY(!multiview)` or warn+fallback.
- **[med]** `Init_status_display::set_clear_color` is dead code (`init_status_display.hpp:44`, `.cpp:66-69`) -- no callers, member is already member-initialized.
- **[med]** `Debug_renderer::is_multiview_active()` lies when `max_view_count == 1`. The multiview overload `ERHE_VERIFY`s `views.size() == max_view_count`, so a `max_view_count = 1` build can call it with one View. `dispatch_compute` keys multiview on `>= 2` -- two predicates disagree. Gate `is_multiview_active()` on `>= 2`.
- **[low]** `padding0_offset` is now dead -- bucket switched to `std::memset` zero-fill; offset is never written. Drop the offset.
- **[low]** `view_camera_stride = view_camera_struct->get_size_bytes()` is fragile -- works because `DebugViewCamera = 112` is a multiple of 16, but adding any non-vec4-multiple field silently breaks indexing. Round up explicitly.
- **[low]** Stale "Phase 1 / Phase 2 / Phase 3" framing in `debug_renderer.cpp:64-75`, `.hpp:77-89`. Doc `debug_renderer_multiview.md` is now canonical -- cut the in-source phase narrative.
- **[low]** `Init_status_display::render_text_overlay` takes `unsigned int texture_layer` but both callers pass `0u` -- parameter is unused.

**No-issues note:** UBO std140 layout checks out for the current `DebugViewCamera`. OpenXR begin_frame/end_frame pairing in `render_present_xr` correctly handles `should_render = false` and `begin_ok = false` branches. The depth-attachment drop is documented and correct given the reverse-depth Text_renderer pipeline.

### Commit 8f90a2b0 -- editor: startup command scripts + scene-builder undoable + multiview polish

**TLDR:** Solid refactor in concept, but the commit message is misleading (the "single OpenXR instance" change isn't in this diff), the new script driver has on-demand-parser ordering hazards, the "undoable" claim leaks side-state on undo, and `Init_status_display::poll_events()` will both discard input and fire from worker threads.

**Findings:**

- **[high]** `Init_status_display::poll_events()` thread-unsafe + drops input -- `init_status_display.cpp:163`. `set_line()` called from taskflow workers, so `render_present()` calls `m_window.poll_events()` from non-main threads. SDL_PollEvent is main-thread-only. The "events accumulate" comment is false: `sdl_window.cpp:738-748` does a ping-pong buffer swap on every `poll_events()` call, **clearing the previous read buffer**. Gate the poll on a main-thread check, or stop pretending events are preserved.
- **[high]** "Undoable scene builder" leaks orphaned side-state -- `scene/scene_builder.cpp:737-753, 705-735`. `add_room()` creates a new `floor_material`, pushes a `Brush` into `m_floor_brushes` and an `ICollision_shape` into `m_collision_shapes` synchronously **before** queuing the `Compound_operation`. Undo removes only the scene-graph node; the material / brush / collision shape stay. Re-running piles up "Floor"s. Similarly `add_cameras()` builds the `Viewport_scene_view` + viewport_window as non-undoable side effect. Wrap material/brush/viewport creation in dedicated operations.
- **[high]** Commit message claims an OpenXR-instance change that is not in the diff. `git show --stat 8f90a2b0` lists no `xr_instance.*`. Remove the bullet or restore the missing change.
- **[med]** simdjson ondemand ordered-access hazard -- `editor.cpp:2069-2075`. Reading `obj["name"]` then `obj["args"]` only works if keys are in that order; `find_field_unordered` not used. A reordered JSON entry silently skips.
- **[med]** 1-MSAA on OpenXR is a workaround, not a fix (`graphics_presets_openxr.json:5`). Underlying multiview-MSAA issue not named or filed. Per CLAUDE.md, either root-cause it or rip out unreachable multiview-MSAA paths.
- **[med]** Per-command `shadow_light_count` clamp lives in `Add_lights_command::apply_args()` (`scene_commands.cpp:132-148`). Future callers that go through `try_call()` without `apply_args()` (key binding, MCP, UI button) will exceed `shadow_light_count`. Move clamp into `Scene_builder::add_lights`.
- **[low]** `find_command()` reads `m_commands` unlocked (`erhe_commands/commands.cpp:37-47`). Justified by "all commands registered during init" -- structural assumption baked into a public method.
- **[low]** `Ambient_light_operation::execute()` overwrites `m_before` on redo (`ambient_light_operation.cpp:24-31`). If any other path mutated ambient_light between undo and redo, redo silently captures the new value as "before" and the operation becomes non-reversible.

**No-issues note:** Multiview wide-line render-style fix (`headset_view.cpp`) is correct. Codegen split (`add_*_args.py`, `make_mesh_args.py`) is clean and matches the typed-config story.

### Commit 9feb3c1a -- erhe + editor: mesh_memory multi-format pool registry + lazy buffer growth

**TLDR:** Decent structural refactor, but ships a real data-corruption bug whenever any pool grows beyond its first block (Triangle_soup uploads target `get_first_buffer()` instead of the allocation's actual block), plus several latent traps (registry race, key collisions across stream layouts, dead config knobs, dead code, big peek/bucket duplication between forward and shadow renderers).

**Findings:**

- **[high]** Triangle_soup / raw enqueue path uploads to the wrong GPU buffer once a pool has >1 block -- `graphics_buffer_sink/erhe_graphics_buffer_sink/graphics_buffer_sink.cpp:46-63`. `enqueue_vertex_data`, `enqueue_index_data`, `enqueue_edge_line_vertex_data` all call `pool->enqueue_data(pool->get_first_buffer(), offset, ...)`. Offsets are local to the allocation's actual block, but upload is routed to block #0. Anything past the first 32 MB block renders garbage. Fix: thread `Buffer*` through the `Buffer_sink::enqueue_*` interface (the `buffer_ready(writer)` overloads already do the right thing via `writer.buffer_range.buffer`).
- **[high]** `Vertex_format_key` ignores stream layout -- `dataformat/erhe_dataformat/vertex_format.cpp:124-181`. Key is per (usage, usage_index), not per stream. Two formats with `position` in different streams produce the same key and alias in the registry. Today the editor only has two formats so no collision yet, but the invariant is wrong. Fold stream index, stride, and element format into the key.
- **[high]** `Mesh_memory::get_or_create_format_pools` is not thread-safe -- `scene_renderer/erhe_scene_renderer/mesh_memory.cpp:56-78`. Inserts into `m_extra_format_pools` without a mutex. Today only called at init; first concurrent caller hits UB.
- **[high]** Bucket key omits `Vertex_input_state*`, contradicting the design doc (`forward_renderer.cpp:142-154`, `shadow_renderer.cpp:41-53`). The renderer uses a single `parameters.vertex_input_state` for the whole call (`shadow_renderer.cpp:247`), so even with multiple formats registered the pipeline is still built for one input. The "exercises the multi-format pool registry end-to-end" claim in `editor.cpp:1023-1026` is misleading.
- **[high]** Pre-flight check removal turns 0-vertex primitives into hard build failures -- `primitive/erhe_primitive/primitive_builder.cpp:107-114`. `Free_list_allocator::allocate(0, ...)` returns a successful zero-byte allocation; the new `range.count == 0 -> build_failed = true` rejects it. Use a different sentinel.
- **[med]** `Buffer_set` duplicated verbatim across `forward_renderer.cpp:142-198` and `shadow_renderer.cpp:41-130`. Two file-local copies that will drift. Hoist into a shared header.
- **[med]** `buffer_pool.cpp:85-94` leaks an `erhe::graphics::Buffer` if the second `push_back` throws. Order pushes so both are reservable up-front, or `reserve` first.
- **[med]** Dead `add_existing_block` API + bare "manual mode" constructor in `buffer_pool.{hpp,cpp}`. No call sites under `src/`.
- **[med]** Per-dispatch SSBO bind in `Content_wide_line_renderer::compute` (`content_wide_line_renderer.cpp:489-496`) -- no dedup on consecutive dispatches that share `(edge_buffer, byte_offset)`.
- **[med]** Doc / code mismatch: `doc/mesh_memory.md:104` claims a `max_format_pools = 32` soft cap; not implemented.
- **[low]** Dead deprecated config knobs `vertex_buffer_size` / `index_buffer_size` (`definitions/mesh_memory_config.py:12-31`).
- **[low]** `notes.md` for `erhe_graphics_buffer_sink` is stale (`Vertex_buffer_entry`, `get_vertex_allocator` no longer exist).
- **[low]** `Buffer_pool` lacks explicit deleted copy/move special members.
- **[low]** `Mesh_memory::vertex_format` reference + `Format_pools::vertex_format` value copy -- two-source-of-truth pattern, re-entrenched.

### Commit 3d570726 -- editor + docs: slim Scene_root, viewport fixes, draw list renderer plan

**TLDR:** Scene_root extraction is mostly sound and the BeginChild leak fix in `viewport_window.cpp` is correct, but the new `Content_library_window` introduces a lifetime regression in `Scene_open_operation::undo`, the `use_draw_list_renderer` config flag and `Renderer_choice` UI are dead scaffolding wired to nothing, and pre-existing latent bugs in the touched code remain unfixed.

**Findings:**

- **[high]** `Content_library_window` is never released on undo -- `asset_browser/asset_browser.cpp:112-117`. `Scene_open_operation::undo()` calls `unregister_from_editor_scenes()` and `remove_browser_window()` but never resets `m_content_library_window`. Result: orphaned "Content Library - ..." window with stale references. Also `m_content_library_window.reset()` in `undo()`.
- **[high]** Dead config + UI knob shipped -- `graphics/definitions/graphics_config.py:60`, `scene/viewport_scene_view.hpp:167`. `use_draw_list_renderer` and `Renderer_choice` parsed / settable but never consumed. `_version` bumped 2 -> 4 (skipping 3) for a renderer not on disk.
- **[med]** `Operations::m_loaded_content_library_windows` is monotonically growing (`operations_window.cpp:217-225`, `.hpp:189`). Vector appended on every `load_scene_file`; nothing prunes when a scene goes away. Ownership for the same scene's UI is now split three ways (Scene_root, browser_window, content_library_window).
- **[med]** `BeginTable` / `EndTable` mismatch reachable under more conditions (`scene_view_config_window.cpp:40-44, 124`). Drops the pre-table `!scene_root` guard but still ignores the BeginTable return. Capture and gate.
- **[med]** Plan doc `draw_list_renderer.md:399-411` references Item_flags `static_transform` (bit 27), `static_material_assignment` (bit 28) -- neither exists. Plan and code are out of phase.
- **[low]** Leftover `<imgui/imgui_internal.h>` include in `scene_root.cpp:32` after drag/drop callbacks moved to `content_library_window.cpp`.
- **[low]** `Content_library_window` has no `delete`d copy/move declarations. Holds an `App_context&` and a `shared_ptr<Item_tree_window>` capturing `[this]` -- a move silently invalidates the lambdas.
- **[low]** Plan doc `scene_root_cleanup.md` already partially obsolete -- describes "Step 1 Dead-code sweep" and "Step 2" in future tense, but the same commit implements them. Plan reads as a half-stale changelog.

**No-issues note:** `viewport_window.cpp` BeginChild fix is correct; `brush.hpp` direct item.hpp include is justified; the `make_shared<Scene_root>(...)` call sites have all been updated to the new 4-argument signature.

### Commit 32ecac80 -- scene_renderer + editor: Standard_shader_variants cache + bucket plumbing

**TLDR:** Variant cache infrastructure and light prefix-partition land cleanly, but the cache key has 5 dead-weight material-texture boolean axes (no shader uses them) that needlessly fragment the cache, and the new `shadow_index == index` policy quietly inflates the required `shadow_light_count` budget so shadow-casters can fall off the end of the render-pass array when many non-shadow lights are present.

**Findings:**

- **[high]** Shadow render passes silently dropped when total lights exceed preset budget -- `light_buffer.cpp:200-225` and `shadow_renderer.cpp:280-289`. Packing sets `shadow_index = slot`, where `slot` is the type-major partitioned UBO index that includes non-shadow lights. `m_render_passes` in `shadow_render_node.cpp` is still sized to `preset.shadow_light_count`. Scene with N shadow-casters + M non-shadow lights of an earlier type bucket: `shadow_index` of N+M, only N passes exist. Bounds check at `shadow_renderer.cpp:281` silently `continue`s. Stealth visual regression. Fix: keep a separate `shadow_index_counter` per type, or size `m_render_passes` to total UBO light count.
- **[high]** Five `USE_*_TEXTURE` boolean axes are dead weight -- `standard_shader_variant.hpp:28-33`. Emitted as `#define`s but the shaders never reference them (still gated by `if (normal_texture.x != max_u32)` sentinels). Cache is fragmented up to 32x for zero compiled-code change.
- **[high]** Bucket variant key OR-merges material caps across primitives with no debug-mode trip -- `forward_renderer.cpp:233`. `compute_bucket_variant_key` ORs `boolean_mask` across every primitive in a bucket. Two materials with disjoint texture sets produce a single permissive variant. Benign today (texture flags unused), but as soon as `#ifdef ERHE_USE_*_TEXTURE` branches exist in the shader, primitives whose material lacks the texture will sample garbage handles.
- **[med]** Material-change invalidate-all is wasteful and Properties-panel live-edit bypasses it -- `material_change_operation.cpp:31-37`, `windows/properties.cpp:940-948`. Bulk-invalidate throws warm variants away; live-slider edits hit stale cache until focus loss.
- **[med]** `vs2022_vulkan.bat` deletion is correct in direction but unrelated to the commit subject. Mixing build-system cleanup into a 1100-line refactor obscures `git log` for both.
- **[low]** `bucket.key` doesn't include `bxdf_model` at this commit's tree state; follow-up commit adds it. Verify cross-commit consistency.
- **[low]** `Shader_monitor::remove` walks every file/entry rather than indexing -- O(files * entries) per call.
- **[low]** `get_or_compile` holds `m_mutex` across `compile_shaders()`/`link_program()` -- driver compile can take 50+ ms, blocks every other thread asking for any variant.

**No-issues note:** X-macro for axes is solid; `unique_ptr` storage for `Reloadable_shader_stages` preserves the Shader_monitor's stable pointer; multiview gate (`!multiview_path`) is correct; light prefix-partition math itself is right.

### Commit f8dab5f8 -- graphics + rendergraph + scene_renderer: cb-targeted Scoped_debug_group for RenderDoc

**TLDR:** The cb-targeted refactor is mechanically sound (the API is now coherent and RAII keeps begin/end balanced across the early-return paths traced), but the new per-bucket `fmt::format` debug labels create a per-frame allocation + global-mutex hot spot that monotonically leaks into the `String_pool` interning singleton, and the Vulkan backend never consults `s_enabled` (pre-existing, made worse by the proliferation of call sites).

**Findings:**

- **[high]** Per-bucket `Debug_label{fmt::format(...)}` leaks into `String_pool` monotonically -- `forward_renderer.cpp:548-561`, `shadow_renderer.cpp:330-341`. `Debug_label(std::string&&)` interns into a global `unordered_set<std::string>` (see `erhe_utility/debug_label.hpp:11-66`) with no eviction. Each frame, every bucket produces a unique string like `"bucket 1/3 prims=2 streams=3 mask=0x..."`. Each construction takes `std::scoped_lock` on a process-wide mutex inside the inner render loop. Gate on `Scoped_debug_group_impl::s_enabled` and use a stack-format buffer feeding the literal-array `Debug_label` ctor.
- **[high]** Vulkan backend records `vkCmdBeginDebugUtilsLabelEXT` unconditionally; if `VK_EXT_debug_utils` isn't loaded the function pointer is null (`vulkan_scoped_debug_group.cpp:34,42`). Pre-existing, but multiplied by the new call sites. Early-return on `if (!s_enabled) return;` to match the GL backend.
- **[med]** `gl_render_pass.cpp:817-822` silently drops `end_render_pass()` GL debug sub-scope. Comment justifies the loss because `end_render_pass()` has no `Command_buffer&`; the right fix is to plumb a cb through.
- **[med]** Metal `Scoped_debug_group_impl` push/pop can desynchronize from the encoder identity (`metal_scoped_debug_group.cpp:17-33`). Ctor sees `get_active_mtl_encoder()`; dtor sees a different encoder if recording switched. Have the impl store the encoder it pushed onto.
- **[low]** `Debug_renderer::begin_frame` debug-group removal loses a RenderDoc-side "begin_frame happened here" marker (`debug_renderer.cpp:399-405,443-446`).
- **[low]** `Texture_heap::reset_heap` / `unbind` API now takes `Command_buffer&` purely for debug bookkeeping (`texture_heap.hpp:32-42`). API now lies about its dependency; consider a debug-free overload.
- **[low]** Vulkan impl caches a `VkCommandBuffer` handle, not the `Command_buffer&`. If a Scoped_debug_group outlives the cb that produced its handle, dtor writes to a recycled handle. Document the lifetime contract.

### Commit 7704e198 -- scene_renderer + editor: Standard_shader_variants refinement + Lazy_shader_handle

**TLDR:** The lazy-compile claim is leaky -- on Vulkan and Metal the pipeline ctor calls `lazy_shader_stages->shader_stages()` synchronously, so the "drop eager compile from `load_programs`" only buys laziness on GL. Vulkan/Metal still compile during `Pipeline_renderpasses` ctor at init. `Cached_shader_handle` does not actually cache; every draw walks an `unordered_map` under a mutex. Several latent regressions / stale docs leak through.

**Findings:**

- **[high]** Lazy compile is eager on Vulkan/Metal -- `vulkan/vulkan_render_pipeline.cpp:23-26`, `metal/metal_render_pipeline.cpp:23-26`. Pipeline ctor calls `lazy_shader_stages->shader_stages()` synchronously to fetch `VkShaderModule` / `MTL::Function`. `Pipeline_renderpasses` is constructed during editor init (`editor.cpp:1180`), so every Vulkan/Metal pipeline still compiles its shader at startup. Either build real `Lazy_render_pipeline`s or document that "lazy" is GL-only.
- **[high]** Metal silently swallows `wide_lines` failure -- `programs.cpp:66-68`. Pre-commit code skipped `add_shader(wide_lines_*)` on Metal (geom shader not supported). New ctor unconditionally registers; Metal's pipeline ctor returns silently on the failed compile. Pipeline objects are constructed but broken. Gate the wide_lines `sv_key(...)` on `use_compute_shader == false`.
- **[high]** Silent semantic change in `standard.frag` emissive -- old: `material.emissive.rgb * material.emissive.rgb * sample.rgb` (squared); new: `material.emissive.rgb [* sampled.rgb]`. Either the old was wrong (this is the silent fix, worth a separate line) or the new is wrong (emissive halved). Confirm + call out.
- **[med]** `Cached_shader_handle` doesn't actually cache (`cached_shader_handle.cpp:29-31, 33-52`). Every call re-acquires `m_cache.m_mutex`. `multiview_shader_stages()` constructs a fresh `Shader_variant_key` on every call (allocates strings + vector). The handle's `shader_stages()` resolves twice per pipeline per frame in some call sites (e.g. `forward_renderer.cpp:448` + `make_temp_pipeline_state` line 97). Memoize the resolved pointer after first success.
- **[med]** Removed runtime sampler-presence checks in `standard.frag` -- old guarded normal/occlusion sampling with `if (normal_texture.x != max_u32)`. New gating is build-time `#ifdef`. If a material lacks a texture but is rendered through a variant compiled with `ERHE_USE_NORMAL_TEXTURE`, the shader samples an undefined `uvec2` handle (UB on Vulkan bindless). Add `ERHE_VERIFY` at bucket-build time.
- **[med]** Stale dependency comment in `editor.cpp:1194-1203` still says `Programs::get_multiview` only returns non-null after `load_programs`. After this commit there is no `load_programs`-populated map.
- **[med]** `Programs::load_programs` is dead code (`programs.cpp:156-177`). Three out of four parameters `/*name*/`ed out; body is essentially empty. Drop the method and `programs_load_task`.
- **[low]** Mutex-vs-contract mismatch -- `shader_variant_cache.hpp:66-68` says "callers must hold the GL context (render thread)" but the cache takes `m_mutex` internally. Pick one.
- **[low]** `Shader_variant_key` is stringly-typed (`shader_variant_cache.hpp:32-49`). A typo in any shader name silently misses; compile fails; Metal/Vulkan returns nullptr from the pipeline ctor. Tiny registry would help.

**No-issues note:** Threading is correctly serialized via cache + monitor mutexes. Member destruction order in `Editor::Impl` is correct. The dedup-log fix (`80b7bdbe`) is a proper root-cause fix.

### Commit 04dc43ba -- editor: expose material editing via MCP server + integration tests

**TLDR:** Functionally the dispatch + queue + `Material_change_operation` plumbing is correct and the test surface is broad, but the new code ships several real problems: zero input-range validation (NaN/inf/negative metallic accepted silently), no authentication on a JSON-RPC endpoint that mutates editor state, ambiguous material/texture lookup by name, an integration-test harness that hard-depends on whichever material happens to be index `0` (test leaves it mutated on failure), and a request-queue back-pressure model that lets a noisy client OOM the editor.

**Findings:**

- **[high]** No request size / queue size limits -- DoS by trivial spam (`mcp_server.cpp:140-145, 268-282`). `httplib` has no payload cap by default. `handle_tools_call` pushes every request into `m_request_queue` with no upper bound. Loopback attacker / buggy client can push gigabyte bodies + flood the queue. Set `set_payload_max_length`, cap queue size, return `-32000` server busy beyond N pending.
- **[high]** No authentication on a mutating endpoint (`mcp_server.cpp:182, 192-223`). Binds 127.0.0.1, but any local process can POST `edit_material`. On Windows / macOS dev workstations, any non-elevated process gets write access. Bearer token from a 0600 file at minimum.
- **[high]** Zero input-range validation -- NaN / inf / negative values flow into `Material_data` (`mcp_server.cpp:1596-1627, 1366-1410`). `try_read_float` accepts `NaN`/`Infinity`. NaN base color propagates into SSBO and breaks lighting. The `if (after == before)` short-circuit at line 1722 is NaN-broken (NaN != NaN), so NaN writes always queue as "change". Use `std::isfinite()` + per-field clamp.
- **[med]** Material / texture name lookup is first-match -- ambiguous when names collide (`mcp_server.cpp:1567-1573, 1442-1457`). Free-form name strings, no uniqueness. When >1 match exists, return `isError` listing candidate ids.
- **[med]** Missing variant-cache invalidation when a queued `edit_material` is shadowed by an "equal value" path. Client legitimately requesting same scalar but new texture pointer can hit `after == before` if pointer comparison happens to equal. Tighten the no-op detection.
- **[med]** Integration tests mutate global editor state with no rollback on failure (`mcp_server_tests.cpp:246-265, 597-714`). Failed assertion leaves material in mutated state. Use RAII save-restore, not best-effort.
- **[med]** Synchronous 5-second blocking wait in HTTP handler (`mcp_server.cpp:284-287`). Small worker pool stalls under modal dialogs / glTF loads. Worse: after timeout the request remains queued and still mutates the material later. Drop expired requests in `process_queued_requests`.
- **[low]** `env_or_int` swallows non-numeric env values (`mcp_server_tests.cpp:43-53`).
- **[low]** `tex_coord` read as `uint32_t` but JSON schema only says `minimum: 0` -- `4294967296` wraps to 0.
- **[low]** Style: trailing-statement one-line `if`s in `mcp_server.cpp:1513-1517`; column drift in dispatch table 319-334.

**No-issues note:** Thread-handoff between HTTP worker and main thread for `action_edit_material` is correct (mutation happens through `operation_stack->queue` from the main thread inside `process_queued_requests`).

### Commit c531aac0 -- editor + graphics: BxDF model + anisotropic brushed-metal materials

**TLDR:** Adds a `Bxdf_model` axis (unlit/isotropic/anisotropic) plus two new material booleans, threads them through the variant cache and bucket key, and forces a Vulkan pipeline-cache flush on `Shader_variant_cache::clear()`. Functionally works but ships dead shader code, an unused energy-control variable, a band-aid `unlit` UBO field, a default-material change that visually regresses every saved scene, and most importantly leaves the same Vulkan stale-handle hazard wide open in `Shader_monitor` live reload.

**Findings:**

- **[high]** Pipeline-cache flush is too narrow -- `Shader_monitor::update_once_per_frame()` destroys and recreates `VkShaderModule`s but never calls `clear_render_pipeline_cache()` (`shader_monitor.cpp:209`). Same recycled-handle hazard the commit "fixes" for `Shader_variant_cache::clear()` applies verbatim to every hot-reload. Invoke from `Shader_stages_impl::destroy_modules()` (or `reload`/`invalidate`), or have `Shader_monitor::update_once_per_frame` call it once after the batch.
- **[high]** `Material_buffer.unlit` field is now a load-bearing band-aid (`material_buffer.cpp:191`). Comment admits it exists "to keep `anisotropic_slope.frag`, `anisotropic_engine_ready.frag`, `circular_brushed_metal.frag`, and `standard_debug.frag` working unchanged." These shaders read `material.unlit == 1` but never look at `bxdf_model`. CLAUDE.md no-band-aid rule: upload `bxdf_model` to the UBO and migrate the legacy shaders, or drop `unlit` and require those shaders to read `bxdf_model`.
- **[high]** Dead `anisotropy_strength` computation in shader -- `standard.frag:96-101`. Local variable computed, scoped, never read. Either wire to `roughness_y = mix(roughness_x, roughness_y, anisotropy_strength)` or delete.
- **[med]** Default-material switch is a user-visible content regression -- `content_library/material_library.cpp:24-30`. Every stock material now created with `bxdf_model = anisotropic_brdf` + `use_circular_brushed_metal = true`. Default of `Material_data::bxdf_model` is `isotropic_brdf`, so old saved scenes binding these by name diverge from regenerated defaults.
- **[med]** No persistence path for `bxdf_model` / `use_*` -- neither glTF export nor any codegen JSON serializer touches the three new fields. `gltf_fastgltf.cpp:1268` reads `KHR_materials_unlit` extension at line 1912/2006 -- but the code path that sets `bxdf_model = unlit` is missing.
- **[med]** Commit message overstates default behavior on cubes. Cubes carry no `custom_attribute_aniso_control`, so `v_aniso_control = vec2(0, 0)`; the circular-brushed-metal block collapses to a no-op visually.
- **[med]** Circular-brush "magnitude" formula has hidden poles and an unprincipled magic number (`standard.frag:88`). `length(v_texcoord) * 8.0`. No comment justifies the `8.0` constant. `normalize(v_texcoord)` is undefined at UV origin (guarded, but neighbors near origin still degrade). UV-seam sign flip rotates the tangent frame discontinuously.
- **[med]** Roughness floor inconsistent between texture / no-texture paths (`standard.frag:117-118` vs 121-123). The 1e-4 clamp only applies when `ERHE_USE_METALLIC_ROUGHNESS_TEXTURE` is defined. MCP / scripted / glTF can write `roughness.y = 0` and the no-texture path bypasses the floor.
- **[low]** Stale doc in `standard_shader_variant.hpp:132-135` still says "the material is not unlit" and "(Future aniso material work will extend this condition.)".
- **[low]** `Bucket_key::operator==` could be defaulted under C++20 spaceship.
- **[low]** `Mesh_primitive_ref::shared_ptr<Mesh>` by value in a hot per-frame vector bumps the refcount on every push. Consider raw `Mesh*` with documented lifetime.
- **[low]** `int current` round-trip cast in `properties.cpp:1029-1031` is unguarded.

### Commit 407c7be3 -- editor + scene_renderer + graphics: pipeline prewarm at init

**TLDR:** Solid first cut at moving shader-variant compile and shadow VkPipeline construction out of the first-frame budget, with good single-source-of-truth for the light tally. But ships `Device::warmup_render_pipeline` as dead code, has a stale "subsequent commits will" header comment, and the comment-coupled "must stay bit-identical" tally in two places is a future-bug magnet rather than a fix.

**Findings:**

- **[high]** `Device::warmup_render_pipeline` is dead code (`device.cpp:121`, `device.hpp:351`). No call sites. The whole point of the commit (per `doc/prewarm.md` "Phase 2: VkPipeline cache") is to populate `vkCreateGraphicsPipelines` at init -- but no caller is wired up. Shadow path uses `Lazy_render_pipeline::get_pipeline_for()` directly.
- **[high]** `prewarm.hpp` documentation is stale (`prewarm.hpp:19-22`). Header says "subsequent commits will extend ... to also cover the shadow depth pass, material/brush preview render paths, and Vulkan VkPipelineCache warmup" -- this commit already does the first two and ships the third as uncalled API. Update to describe what it does today.
- **[med]** Two-source tally with "must stay bit-identical" is a foot-gun, not a fix (`standard_shader_variant.cpp:222-256` and `light_buffer.cpp:189-207`). CLAUDE.md no-band-aid: have `Light_projections::apply` call the helper or stash its scratch into the result, instead of comment-locking two impls. The `doc/prewarm.md` "remaining work" proposes a "divergence detector" -- that's also a band-aid.
- **[med]** Prewarm only walks `primitive_mode=polygon_fill` (`prewarm.cpp:148`, `scene_preview.cpp:296`). Editor renders composition passes with `polygon_fill`, `edge_lines`, `corner_points`, `corner_normals`, `polygon_centroids`. Either walk all modes or assert / document that the others don't opt into `uses_standard_variants`.
- **[med]** Multiview view-count hardcoded to `{0, 2}` (`prewarm.cpp:91`). Acknowledged in doc, but unguarded for foveated / quad-view. Source from `Headset_view` or assert `view_count <= max_view_count`.
- **[med]** `Device::warmup_render_pipeline` is a silent no-op on GL and Null (`gl_render_pipeline.cpp:5-8`, `null_render_pipeline.cpp:5-7`). Future callers using a GL preset will see prewarm appear to work but do nothing. Log at info on no-op backends. Metal is NOT a no-op -- doc understates Metal cost.
- **[low]** All glslang compiles run serially on the prewarm-calling thread (mutex in `compile_locked`). ~565 ms for 12 variants on Quest -> roughly 47 ms each, serial. Future taskflow phase, with permission per CLAUDE.md.
- **[low]** No invalidation on material texture changes -- UI texture binding flips USE_*_TEXTURE in the (currently dead-weight) key; on the day those gates earn their place, prewarm covers only the as-loaded state.
- **[low]** `App_context::OpenXR` (used at `prewarm.cpp:88`) is PascalCase -- pre-existing naming-convention violation propagated into new files.

**No-issues note:** Shadow-pass dedup via `Lazy_render_pipeline*` pointer identity is correct. Gate (`uses_standard_variants && vertex_format != nullptr`) is bit-identical to runtime `variant_lookup_active` predicate (`forward_renderer.cpp:506-511`). Call-site placement after `run_startup_script()` and before `wait_idle` is right.

### Commit 82e4b778 -- editor: startup script seeds Johnson solids + doc refresh

**TLDR:** The one-line JSON swap is clean and valid, but `doc/building.md` is the opposite of "refreshed" -- it adds typos, broken paths, and a "CMake Presets" section that directly violates CLAUDE.md's explicit prohibition on documenting `cmake --preset`.

**Findings:**

- **[high]** Documents `cmake --preset` (`doc/building.md:48-61`). CLAUDE.md is explicit: "Do not invoke `cmake --preset ...` directly. ... Plans, scripts, and docs should not reference `cmake --preset`." Delete the entire "CMake Presets" subsection. Same rule violation appears under "Visual Studio Code" (line 67, "CMake: Select Configure Preset") and "CLion" (line 73).
- **[high]** macOS / Linux commands use Windows path separators -- `doc/building.md:99` (`scripts\configure_xcode_metal.sh` in a `bash` fenced block); line 90 (Linux now skips `scripts/configure_ninja_linux.sh` entirely, falls back to ad-hoc `cmake -B build`, contradicting CLAUDE.md's "Always use the `scripts/` build scripts on Linux").
- **[med]** Two typos -- `doc/building.md:6`: "Exceptionionally" (should be "Exceptionally") and "inclouded" (should be "included"). Paragraph is also less informative than the line it replaced.
- **[med]** Broken table cell for `ERHE_GRAPHICS_API` -- `doc/building.md:128`. Missing comma between `(macOS only)` and `none`. `vulkan` added despite CLAUDE.md "not yet ready" caveat; previous "Vulkan preset exists, ... is nowhere near usable" caveat got deleted.
- **[med]** Meta Quest section is a trap (`doc/building.md:114-122`). Shows only `scripts\build_android.bat`; omits install/launch + the controllers-required protocol that CLAUDE.md describes at length.
- **[low]** Windows section drops valid compiler info (`doc/building.md:12`).
- **[low]** Stray double space (`doc/building.md:80`).

**Verified clean:** `commands.json` change parses (JSON valid, all five arg names match `Make_mesh_args` struct fields), `scene.add_johnson_solids` is a real registered command (`scene_commands.cpp:179`), `Scene_builder::add_johnson_solids` exists (`scene_builder.cpp:818`). ASCII-only check on both files passes.

## Cumulative architectural review

### TL;DR

The branch lands a coherent multi-pillar feature set (Vulkan multiview, OpenXR-SDK refresh, mesh-memory + variant-cache + prewarm) and the seams between pillars mostly fit. But the squash makes visible several cross-system half-finished contracts: the lazy-shader story is half-lazy at best, the variant cache covers only the `standard` shader while every legacy shader still lives in eager Cached_shader_handles, light-count tally code is intentionally duplicated with a "must stay bit-identical" comment, an `ERHE_GRAPHICS_API_NULL` typo slipped through the API rename, and `Init_status_display` drives OpenXR from a constructor that itself runs while taskflow workers are pushing status lines into the same `m_lines` vector without a mutex.

### Architectural / cross-cutting findings

- **[high] "Lazy" shader handles are not actually lazy on Vulkan/Metal.** `Lazy_shader_handle` (`src/erhe/graphics/erhe_graphics/lazy_shader_handle.hpp`) + `Cached_shader_handle` (`src/erhe/scene_renderer/erhe_scene_renderer/cached_shader_handle.cpp:28`) are advertised in `programs.cpp` ("Phase 3: lazy startup... nothing compiles here") and in `programs.hpp:99` ("startup becomes truly lazy"). But `Render_pipeline_impl::Render_pipeline_impl` on both Vulkan (`vulkan_render_pipeline.cpp:21-26`) and Metal (`metal_render_pipeline.cpp:21-26`) resolves the handle synchronously via `create_info.lazy_shader_stages->shader_stages()` -- it compiles the shader the moment a `Render_pipeline` is constructed. Every `Pipeline_renderpasses` member in `app_rendering.cpp` is constructed at editor init through `Lazy_render_pipeline`, but Lazy_render_pipeline does eventually realize each pipeline; on the GL backend this works out, but on Vulkan the prewarm walks every Render_pipeline anyway. Net result: the "lazy" story buys you nothing on Vulkan beyond complexity, and the OpenGL "lazy" story is purely a deferral of GL shader-object creation, not glslang. Either commit fully to lazy (move the resolve into `set_render_pipeline_state` on Vulkan with cached VkPipeline reuse) or drop the abstraction and call it `Cached_shader_handle` without the lazy claim.

- **[high] Variant-cache invalidation is wired to one of three material-edit paths.** `Material_change_operation::execute/undo` (`material_change_operation.cpp:42-54`) calls `standard_shader_variants->clear()`. MCP's `action_edit_material` routes through Material_change_operation so it inherits the invalidation. But the Properties panel's `material_properties()` ImGui block (`properties.cpp:1023-1037`) live-edits `data.bxdf_model`, `data.use_circular_brushed_metal`, `data.use_aniso_control` directly through `ImGui::Combo`/`Checkbox` on `material->data` -- no operation, no invalidation. A user toggling "Circular Brushed Metal" via the Properties window flips a variant axis that `compute_standard_variant_key` reads; the bucket's old (key->compiled-shader) mapping is now wrong but cached, and the next frame renders with the previous shader until something else invalidates. Either route Properties through an undoable Material_change_operation (consistent with the rest of the editor) or expose a non-undoable `App_context::invalidate_standard_variants()` and call it from the live-edit path.

- **[high] Two-source-of-truth light-count tally, deliberately.** `compute_standard_variant_light_counts` (`standard_shader_variant.cpp:745-779`) is "the single-source helper" used by prewarm and previews. But `Light_projections::apply` (`light_buffer.cpp:179-189`) computes the same six counts inline because it needs the per-type-and-shadow scratch arrays for slot assignment, and ships with a comment "Any change here must also update the helper." This is a maintenance bomb: a future contributor changing the slot-assignment order (e.g. shadow-mapped suffix instead of prefix) sees the inline scratch as the obvious change site and forgets that prewarm now keys on a stale count. Share machinery: have `apply()` call the helper for the count snapshot and only use the scratch for slot assignment, or surface a single `Light_layer_partition` struct that returns both.

- **[high] Shader_monitor hot reload bypasses the Vulkan pipeline-cache flush.** `Shader_variant_cache::clear()` (`shader_variant_cache.cpp:367-382`) carefully calls `m_graphics_device.clear_render_pipeline_cache()` to drop pipelines hashed against VkShaderModule pointers that the driver may recycle. The comment is accurate. But `Shader_monitor::poll_thread` reloads `Reloadable_shader_stages` in place when a watched file changes (`shader_monitor.cpp`), destroying and recreating the underlying VkShaderModule -- and there is no callback path from `Reloadable_shader_stages::reload` into `Device::clear_render_pipeline_cache`. A live shader edit therefore leaves stale VkPipelines keyed by the (possibly-recycled) old module handle, exactly the hazard that motivated `clear_render_pipeline_cache` in the first place. Either `Reloadable_shader_stages::reload` should call `clear_render_pipeline_cache`, or the variant cache should be the only thing that creates Reloadable shader stages and the policy lives there.

- **[high] Init_status_display races on a vector of strings.** Constructor parameter comment block in `editor.cpp:919-928` openly states: "parallel init's `init_message` callbacks fire from taskflow worker threads ... concurrent `set_line()` calls from multiple workers are serialized only by the lack of contention in practice; adding a mutex around set_line is the right fix if that ever changes." `set_line` (`init_status_display.cpp:57-64`) calls `m_lines.resize`/`m_lines.at` then `render_present()` which iterates `m_lines` in the render loop; `render_present_xr` even drives `xrBeginFrame`/`xrEndFrame` on whatever worker happens to call. This is acknowledged-but-unfixed in code that this branch touches; "happens to work" is a band-aid by the project's own no-band-aid rule. The fix is one `std::mutex` around `m_lines`/`m_clear_color`/`render_present`.

- **[med] Variant cache covers `standard` only -- legacy lit shaders still eager.** `app_rendering.cpp` migrated every `Pipeline_renderpasses` member from `&programs.circular_brushed_metal.shader_stages` to `.lazy_shader_stages = &programs.standard` with `uses_standard_variants = true`. But `Programs::Programs` still constructs Cached_shader_handle members for `anisotropic_slope`, `anisotropic_engine_ready`, `circular_brushed_metal`, and 28 `standard_debug` variants (`programs.cpp:124-165`). These are name-keyed singletons in the cache, not part of the `Standard_variant_key` space, so material-edit-driven `clear()` recompiles them too (see the "hammer-style invalidation" comment in `standard_shader_variants.cpp`). The branch promises a per-material variant story but only the new `standard` shader actually participates; `standard_debug` is what the debug-viz dropdown drives and never gets a bucket-correct variant. `doc/shader_variants.md:35-42` is explicit that adopting non-standard shaders is "out of scope," but the consequence -- a cache `clear()` triggered by any property edit recompiles 30+ eager shaders -- is not called out.

- **[med] Hardcoded `{0, 2}` view counts hide foveated / quad-view today.** `prewarm.cpp:89-99` writes `view_counts.push_back(0u); view_counts.push_back(2u);` under `if (context.OpenXR)`. `Xr_session::create_swapchains` (`xr_session.cpp:541-553`) gates multiview on exactly `views.size() == 2`, and `Programs::multiview_count` clamps to `program_interface.config.max_view_count`. The whole stack therefore assumes stereo. `Headset_config` exposes a `quad_view` knob for Varjo headsets and `doc/multiview.md:13` calls scaling-out a non-goal. Right shape: read `max_view_count` once at init, prewarm for `{0, max_view_count}`.

- **[med] Duplicated bucket / `peek_mesh_buffers` machinery in forward + shadow.** `forward_renderer.cpp` introduces `Buffer_set`, `peek_mesh_buffers`, `Bucket_key` / `Bucket`, `bucket_primitives_by_buffers_and_variant` (~200 LOC). `shadow_renderer.cpp:36-119` re-implements `Buffer_set`, `peek_mesh_buffers`, `Bucket`, and `bucket_meshes_by_buffers` with only the variant-key part stripped. They have the same assertions ("primitives of a mesh must share buffer set"), the same `Buffer_set::operator==`, the same in-place linear search through `vector<Bucket>`. Forward's `bucket_primitives_by_buffers_and_variant` could collapse onto a smaller `bucket_meshes_by_buffer_set` reused by shadow if the variant signature were a separate sub-key composition. Shared header in `erhe_scene_renderer/` (or `erhe_renderer/`) would be the natural home.

### Missed cleanup opportunities

- `Programs::load_programs` (`programs.cpp:157-211`) is now a no-op apart from a single `init_message("")` call. The taskflow node in `editor.cpp:949` can be folded away with the method.
- `Device::warmup_render_pipeline` (`device.cpp:121-126`, `device.hpp:351`) has zero call sites. `forward_renderer.hpp:131` and `prewarm.hpp:22` reference it as future home of VkPipeline-cache warmup but `prewarm_all` does not call it. Either wire prewarm to it or remove the method.
- `Init_status_display::set_clear_color` (`init_status_display.cpp:66-69`) has no caller. `Scene_preview::set_clear_color` (different class) does have callers; the Init_status one is a dead member.
- `Buffer_pool::add_existing_block` (`buffer_pool.cpp:39`, `buffer_pool.hpp:81`) has no caller; the header's class comment is the only thing that references it. From an earlier design where the pool's growth path went through `acquire_block`.
- `Cached_shader_handle::multiview_shader_stages` rebuilds the multiview `Shader_variant_key` from scratch on every call (`cached_shader_handle.cpp:39-58`), commented "wasteful but cheap." If the call ever lands on a hot path, cache alongside `m_single_view_key`.
- The `unlit` UBO field in `Material_data` is still allocated and written by `material_buffer.cpp:62, 191-225` purely because `anisotropic_*`, `circular_brushed_metal`, and `standard_debug` shaders read it. Once those land on `bxdf_model`-keyed variants the UBO slot can go.
- `Programs::m_handles_by_name` (`programs.cpp:170-218`) is documented as "legacy name-keyed multiview lookup that app_rendering.cpp still uses through get_multiview_stages helper" -- but `get_multiview_stages` was removed in the same commit and the map plus `Programs::get_multiview` are dead.

### Documentation drift

- **`doc/prewarm.md`** -- The "Phase 2: VkPipeline cache" section (lines 53-82) documents `Device::warmup_render_pipeline` as if `prewarm_all` calls it. It does not (only `Shadow_renderer::prewarm_pipelines` and `Forward_renderer::prewarm_standard_variants` are invoked). Mark "future work" or wire the call.
- **`doc/shader_variants.md`** -- Lines 1-12 say "Infrastructure landed; render-time adoption follow-up," but lines 33-42 list adoption work that has actually shipped on this branch (Scene_preview, Headset_view via the multiview path). The "Remaining adoption work" paragraph is stale.
- **`doc/multiview.md`** -- Accurate for current code, but lines 9-14 promise "Per-eye fallback path stays in place for ... Vulkan devices without multiview" and then assumes the per-eye path is correct on OpenGL. The branch's actual GL coverage is the desktop-mirror window (single view); GL OpenXR multiview is not wired and there is no warning in the doc.
- **`doc/scene_root_cleanup.md`** -- Step 1 and 2 are described as future work ("Recommended approach (in execution order)"), but commit `3d570726` already shipped both (the `Content_library_window` files exist; `Scene_root::m_camera`/`m_camera_controls` are gone). Reads as plan; should be marked done or rewritten as a postmortem.
- **`doc/draw_list_renderer.md`** -- Plan-as-future-work, correctly so; clearly marked "first draft -- iterate before implementing."
- **`doc/debug_renderer_multiview.md`** -- Marked "Implemented," matches the code.

### Naming / API drift

- `ERHE_GRAPHICS_API_NULL` in `src/erhe/graphics/erhe_graphics/render_pipeline.cpp:12` is a typo of `ERHE_GRAPHICS_API_NONE` (the rest of the tree uses NONE; CMakeLists.txt only defines NONE). The headless build for `render_pipeline.cpp` falls through silently because none of the four backend conditions match. Rename-commit residue.
- `Cached_shader_handle` is a misleading name -- it does cache on first access but caches *into a shared cache object*. `Variant_handle` or `Shader_handle` would describe what callers experience.
- `Headset_config.depth` (enables `XR_KHR_composition_layer_depth`, used in `xr_instance.cpp:374`) vs `Headset_config.swapchain_depth` (creates a depth-stencil swapchain, used in `xr_session.cpp:534`) are dangerously similar names for orthogonal concepts. Rename to `composition_depth_layer` / `swapchain_depth_attachment` or document the distinction in `headset_config.py`'s currently-empty `short_desc` fields.

### Cross-commit broken contracts

- Commit `407c7be3` (prewarm) advertises Phase 2 = "VkPipeline cache via `Device::warmup_render_pipeline`" but `prewarm_all` never calls it; commit `7704e198` (variant-cache refinement) was where this would have naturally landed and did not.
- Commit `d7aa7f3d` (`ERHE_GRAPHICS_API` rename) missed `render_pipeline.cpp:12`; commit `87eb2863` (multiview foundation, same file) did not catch it either because the file already had multiple `#ifdef`s switched, and the `NULL` branch only fires in headless builds.
- Commit `32ecac80` (variant cache + bucket plumbing) added `Material_change_operation::clear()` but commit `c531aac0` (BxDF model) added the Properties-panel `bxdf_model` combo and `use_circular_brushed_metal` / `use_aniso_control` checkboxes without routing them through the operation. The two commits agree on the variant axes; they disagree on who invalidates.
- `_version` bumped in `config/editor/erhe_graphics.json` to version 4 with no migration logic to point at. CLAUDE.md says omit `_version` at 1 and only bump when migrating -- a whole-config v4 bump for an unused knob is the kind of pattern that needs justifying.

### Conclusion

The riskiest area is the **variant-cache lifetime story**: a hot-reload pipeline-cache invalidation gap (`Shader_monitor` reload not flushing), a Properties-panel material-edit gap that leaves the cache live with the wrong shader, and a tally-duplication landmine in `Light_projections::apply` are three independent ways to render stale shaders without anybody noticing until a frame looks wrong.

The **strongest part** of the change is the multiview foundation: the capability gate in `Xr_session::create_swapchains` is conservative, the per-eye fallback is preserved end-to-end, and `Headset_config` consolidation cleanly retires `Xr_configuration` so OpenXR-SDK 1.1.59 + validation layers + APK packaging all land as one consistent slice.

Tightening the variant-cache invalidation perimeter and folding the now-dead `load_programs` / `warmup_render_pipeline` / `Init_status_display::set_clear_color` / `add_existing_block` / `m_handles_by_name` would buy the most signal for the smallest follow-up commit.

## High-severity summary

The following are the high-severity findings concentrated for triage; lower-severity findings are in the per-commit sections above.

| # | Location | Issue |
|---|---|---|
| 1 | `camera_buffer.cpp:122-140` | Single-camera update uploads uninitialized bytes when `max_view_count >= 2` (cube_renderer / texel_renderer paths). |
| 2 | `render_pipeline.cpp:12` | `ERHE_GRAPHICS_API_NULL` typo (should be `_NONE`) breaks headless backend compile. |
| 3 | `CMakeLists.txt:127` | `ERHE_TARGET_QUEST_STANDALONE` defined, referenced nowhere -- dead. |
| 4 | `xr_instance.cpp:316-321` | Overly broad VUID silencer for `xrStructureTypeToString-*` hides future real VUIDs. |
| 5 | `android-project/app/build.gradle:171-195` | Gradle task does not invalidate cached `libVkLayer_khronos_validation.so` on `validationLayerVersion` bump -- stale layer ships. |
| 6 | `editor/xr/headset_view.cpp:612-644` | Multiview wide-line feed silently ignores `Composition_pass::get_render_style` and the `selection_outline` breathing animation. |
| 7 | `editor.cpp:921-928`, `init_status_display.cpp:294-425` | OpenXR thread-safety race in `set_line` -> `render_present_xr` -- documented, not fixed. |
| 8 | `mcp/mcp_server.cpp:147-171` | `Mcp_server::stop` TOCTOU race traded, not fixed. |
| 9 | `init_status_display.cpp:163` | `poll_events()` from taskflow workers -- SDL is main-thread-only; the ping-pong buffer also drops accumulated input. |
| 10 | `scene/scene_builder.cpp:737-753, 705-735` | "Undoable" scene-builder leaks materials / brushes / collision shapes / viewports on undo. |
| 11 | Commit `8f90a2b0` message | Claims an OpenXR-instance change that is not in the diff. |
| 12 | `graphics_buffer_sink.cpp:46-63` | Triangle_soup / raw enqueue paths upload to `get_first_buffer()` instead of the allocation's actual block -- corrupts data once pool >32 MB. |
| 13 | `dataformat/vertex_format.cpp:124-181` | `Vertex_format_key` ignores stream layout; future formats can alias and corrupt. |
| 14 | `scene_renderer/mesh_memory.cpp:56-78` | `get_or_create_format_pools` not thread-safe. |
| 15 | `forward_renderer.cpp:142-154`, `shadow_renderer.cpp:41-53` | Bucket key omits `Vertex_input_state*` -- single-pipeline renders defeat the multi-format design. |
| 16 | `primitive_builder.cpp:107-114` | Zero-vertex primitives now fail to build (pre-flight removal). |
| 17 | `asset_browser/asset_browser.cpp:112-117` | `Content_library_window` never reset on scene-open undo -- orphan window. |
| 18 | `graphics/definitions/graphics_config.py:60`, `viewport_scene_view.hpp:167` | `use_draw_list_renderer` + `Renderer_choice` config / UI wired to nothing. |
| 19 | `light_buffer.cpp:200-225`, `shadow_renderer.cpp:280-289` | `shadow_index = slot` over-counts non-shadow lights -- shadow passes silently dropped when render_passes array is too small. |
| 20 | `standard_shader_variant.hpp:28-33` | Five `USE_*_TEXTURE` boolean axes are dead-weight -- 32x cache fragmentation for zero shader effect. |
| 21 | `forward_renderer.cpp:233` | Bucket variant key ORs material caps across primitives -- a future `#ifdef ERHE_USE_*_TEXTURE` branch will sample garbage. |
| 22 | `forward_renderer.cpp:548-561`, `shadow_renderer.cpp:330-341` | Per-frame `fmt::format` debug labels intern into global `String_pool` -- unbounded growth, global-mutex per bucket. |
| 23 | `vulkan/vulkan_scoped_debug_group.cpp:34,42` | Records `vkCmdBeginDebugUtilsLabelEXT` unconditionally -- crashes on devices without `VK_EXT_debug_utils`. |
| 24 | `vulkan_render_pipeline.cpp:23-26`, `metal_render_pipeline.cpp:23-26` | "Lazy" compile is eager on Vulkan/Metal -- pipeline ctor calls `shader_stages()` synchronously. |
| 25 | `programs.cpp:66-68` | Metal silently swallows `wide_lines` failure (geom shader unsupported) -- broken pipelines constructed silently. |
| 26 | `standard.frag` emissive | Silent semantic change between old (squared) and new (linear) emissive -- not called out in commit message. |
| 27 | `mcp_server.cpp:140-145, 268-282` | No payload / queue cap on MCP -- trivial DoS. |
| 28 | `mcp_server.cpp:182, 192-223` | MCP has no authentication; any local process mutates editor state. |
| 29 | `mcp_server.cpp:1596-1627` | No `isfinite()` / range validation on material edits -- NaN propagates to SSBO and breaks lighting. |
| 30 | `shader_monitor.cpp:209` | Pipeline-cache flush is only on `Shader_variant_cache::clear()`; hot reload also destroys VkShaderModules but does NOT flush -- recycled handles can alias to stale pipelines. |
| 31 | `erhe_scene_renderer/material_buffer.cpp:191` | `unlit` UBO field kept "for legacy shaders" -- band-aid that disagrees with `bxdf_model` axis. |
| 32 | `standard.frag:96-101` | `anisotropy_strength` computed and never read; documented "X is used to modulate anisotropy" has zero effect. |
| 33 | `device.cpp:121`, `device.hpp:351` | `Device::warmup_render_pipeline` defined, never called. Prewarm "Phase 2" advertised but absent. |
| 34 | `prewarm.hpp:19-22` | Comment says "subsequent commits will" implement work that this very commit already shipped. |
| 35 | `doc/building.md:48-61` | Refresh adds a `cmake --preset` section that CLAUDE.md explicitly forbids. |
| 36 | `doc/building.md:99, 90` | macOS / Linux blocks use Windows backslashes / skip the required `scripts/` wrappers. |

## Themes that recur

- **Band-aid acknowledgement instead of root-cause fix** -- pattern repeats across `Init_status_display::set_line` race (acknowledged, not fixed), `Material_buffer.unlit` (kept to avoid migrating legacy shaders), and the duplicated light-tally helper (comment-pinned bit-identical instead of single-source).
- **Dead code shipped as scaffolding** -- `Device::warmup_render_pipeline`, `Init_status_display::set_clear_color`, `Buffer_pool::add_existing_block`, `Programs::load_programs`, `use_draw_list_renderer`, `Renderer_choice`, `anisotropy_strength`.
- **"Lazy" / "Cached" naming that does not match behavior** -- `Lazy_shader_handle` resolves synchronously on Vulkan/Metal pipeline ctor; `Cached_shader_handle` walks an `unordered_map` under a mutex on every draw.
- **Bucket / variant key misses fields it depends on** -- bucket key omits `Vertex_input_state*` and (at this tree state) `bxdf_model`; variant key has five always-true `USE_*_TEXTURE` axes.
- **Hardcoded `view_count` 2** -- multiview prewarm view counts, content_wide_line padding, debug_renderer `is_multiview_active` predicate all assume two views.
- **Plan docs treated as in-flight when they are already shipped** -- `scene_root_cleanup.md`, `prewarm.md`, `draw_list_renderer.md`, `shader_variants.md` describe steps in future tense that the same commit landed.
