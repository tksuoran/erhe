# 2026-09-30 audit: medium-term set and follow-ups

Status: in progress

The 2026-09-30 audit is `doc/reference/audit_erhe_2026_09_30.md` (options
in section 8, the slice reports beside it); its "Status of the section 8
options" table records what has been worked, by commit. Near-term items 1-9
are done except the parts listed under "Open parts of worked items" below.
From the medium-term list (items 10-20) the first set was 10, 18 and
the include-diet half of 12: item 10 closes the only known GPU correctness hole
and was an open agfx-port finding, item 18 dominates Quest first-frame time,
and the include diet is the precondition of the `erhe_graphics` interface /
backend split. Items 11 and 13-16 are multi-week redesigns, 17 is CI work, 19-20 wait
on culling.

## Done

- Item 10: per-subresource Vulkan layout tracking (4fc17d988) and the
  `Texture_location` / `Buffer_texel_location` blit region types (1d5d45f28,
  narrowing fix 51dfc09fb), and the three OpenGL backend failures the GPU
  tests found while testing it (one-sample multisample target, NVIDIA
  `texelFetch` 3D driver defect, `Device_impl` member destruction order).
- Item 18, shader pipeline persistence, for glslang and Vulkan: the SPIR-V
  cache key hashes the compile settings and entries are written atomically,
  and the `VkPipelineCache` is persisted per device identity
  (`doc/erhe/vulkan_backend.md` "Shaders" and "Pipeline cache persistence").
- Item 12, first half, the include diet (`doc/erhe/graphics.md` "Header
  dependencies"). Measured on the Linux Vulkan Debug tree with clang 18:
  `device.hpp` preprocesses to 75k lines (was 177k); of the tree's 2209
  translation units the ones depending on `shader_monitor.hpp` went from 358
  to 35, on `frame_time_recorder.hpp` from 360 to 31, on `math_util.hpp` from
  397 to 213, on `texture.hpp` from 267 to 136. `texture.hpp` itself stays at
  131k lines: `Texture` is an `erhe::Item`, so the rest is `item.hpp` (item
  11). No header outside `src/erhe/graphics/` includes a backend header, so
  the interface / backend split has nothing to untangle first.
- Item 12, second half, the interface / backend split (`doc/erhe/graphics.md`
  "Interface and backend targets"): `erhe_graphics_interface` is an object
  library of the public headers and the backend-free translation units with
  only the neutral dependencies, and `erhe_graphics` holds the selected
  backend plus the pimpl bridges and archives the interface objects, so
  consumers still link `erhe::graphics` only. The rebuild half of the item's
  stated benefit was already delivered by the include diet: measured before
  the split, a backend-only edit recompiled objects inside the library only
  (1 for `vulkan_device.cpp`, 26 for `vulkan_device.hpp`) and no consumer
  library; the split adds the compile-time check that interface sources
  cannot include a backend header. The null-backend CI build existed
  already (`Windows (VS 2026 / headless)`).

## Next

Selected 2026-10-04, in this order. All three can be built and verified on
Windows; the macOS-only items under "Open parts of worked items" wait for a
macOS session.

1. **Shadow culling on the draw-list entry AABBs** (finishes item 9; read
   `doc/plans/draw_list_renderer.md` item 1 and `doc/erhe/draw_list_renderer.md`
   Q6). `Shadow_renderer` reads the caster / receiver AABBs of the frustum
   fit from the entries instead of walking every content mesh per shadow
   render, and the shadow lists cull casters against each light's frustum
   (point lights per cube face), with the same conservative planes-only test
   and pass-decision mask the color path uses (e476817cf). Skinned entries
   and invalid AABBs are never culled. The in-place mesh-component drag that
   leaves bounds stale (same plan item) is part of this task, because shadow
   culling makes the stale bounds visible as missing shadows. Verify: the
   shadow gate (`doc/erhe/shadows.md` "Shadow verification") stays at
   0 FAIL, the `Shadow_gpu_test` and `Mcp_test` shadow cases pass, and a
   culled-count statistic per shadow pass (next to `Draw_statistics::culled_count`)
   shows casters outside the light frustum skipped.
2. **Editor bits out of `Item_flags` / `Item_type`** (item 11; read the
   scene slice report sections 1.1 and 6 item 2, `doc/erhe/item.md`).
   A reserved application bit range with application-registered label
   tables replaces the about 22 editor-only flags and 24 editor-only type
   indices; `Item_host::hosted_selection` moves into the editor's
   `Selection`; the flag and type tables split out of `item.hpp` and the
   geogram and profiler includes leave `primitive/build_info.hpp` and
   `item_host.hpp`. The glTF `ERHE_*` extensions serialize flags by name
   (`gltf_item_flags.hpp`), so saved scenes must load unchanged: verify with
   the glTF round-trip script and `erhe_gltf_tests`, plus the item, scene and
   USD unit tests (USD `purpose` derives from four of the editor bits).
   Measure the `texture.hpp` / `node.hpp` preprocessed size before and after
   (`doc/erhe/graphics.md` "Header dependencies" has the method).
3. **CI hardening** (the open half of item 17; read the infra slice report
   section 10 items 5-8, `doc/testing.md`, `.github/workflows/`). A Linux
   Clang matrix entry building with AddressSanitizer and UBSan
   (`ERHE_USE_ASAN` exists for GCC / Clang since 091b5879f; add a UBSan
   option beside it) running the deviceless tests; `-Werror` on Clang / GCC
   per target, leaf libraries first; version embedding (`erhe_version.hpp`
   from `project(VERSION)` plus `git describe`, logged at startup next to
   the dependency commits). Software-Vulkan (lavapipe) GPU tests in CI are
   a separate step after these. The workflow changes can only be verified by
   a CI run, which needs the user to push; build the flags locally first
   (a Clang tree on Windows covers `-Werror` and ASan).

## Open parts of worked items

- Item 18, Metal half: a persisted `MTLBinaryArchive` for the Metal backend
  (the `Device::warmup_render_pipeline` comment in `device.hpp` names the
  opt-in); needs a macOS session to build and verify.
- Item 6, Metal half: GPU timers (read 0), `blit_framebuffer` (fatal) and
  swapchain resize on Metal; needs a macOS session.
- Item 9, shadow half: `doc/plans/draw_list_renderer.md` item 1.

Each item is one commit with builds, tests and docs as `AGENTS.md` requires,
and each commit gets a Fable review at medium effort.

## Follow-ups found by review

The three latent layout-tracking defects of 4fc17d988 (clearing a texture
view cleared the whole image, a stencil-only multisample resolve recorded
no layout for its target, `generate_mipmaps` on a level view sized the
chain from the image's level 0) and the dead `load_texture()` of hextiles
are fixed; each defect has a `erhe_graphics_gpu_tests` case that reaches it
(`doc/erhe/vulkan_backend.md` "Image layout tracking").

`Gpu_test.device_up_clean` fails on machines whose Vulkan validation layer is
older than the pinned Vulkan headers (an unknown `sType` in the device create
`pNext` chain); that is a machine setup issue (`doc/agents/linux.md`), not a
code defect.

The null backend builds but the editor does not run on it: the null
`Device_impl::get_command_buffer()` is a stub that returns a dereferenced
null pointer (`null_device.cpp`, "iteration target"), and the editor
constructor calls `begin()` on the result. The CI headless job builds that
configuration and runs no editor, so this is an open item of the null
backend, found while verifying the interface / backend split on a Linux
`ERHE_GRAPHICS_API=none` tree.
