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
- Item 11, editor bits out of `Item_flags` / `Item_type`
  (`doc/erhe/item.md` "Application bits", `doc/editor/coding_rules.md`
  "Editor item flags and types"): the 22 editor-only flag bits and the 22
  editor-only type indices are the editor's (`src/editor/editor_item_bits.hpp`)
  in the reserved application ranges, registered with their labels and
  glTF names at startup; `Item_host::hosted_selection` is `Selection`'s;
  the flag and type tables are `item_flags.hpp` / `item_type.hpp`, the
  profiler include left `item_host.hpp` (`profile_mutex.hpp`) and geogram
  left `build_info.hpp`. Measured with MSVC on the Windows ninja tree
  (`/EP`, no PCH): `texture.cpp` 174294 -> 174209 and `node.cpp`
  180292 -> 180207 non-empty preprocessed lines - the table split is not a
  diet, because what `item.hpp` costs its consumers is the property system
  and the standard library behind `Item_base`, not the tables.
- Item 17, CI hardening (`doc/testing.md` "CI", `doc/building.md`):
  `ERHE_USE_UBSAN` beside `ERHE_USE_ASAN` in every GNU-style toolchain
  file, a Linux Clang ASan+UBSan matrix entry running the deviceless set
  with `halt_on_error`, erhe's warning set per erhe target on
  Clang, GCC and AppleClang, `ERHE_WARNINGS_AS_ERRORS` (default on) for
  the MSVC `/WX`, and `erhe::version` (`doc/erhe/version.md`) logged as the
  editor's first startup line beside the dependency commits. `-Werror` on
  Clang, GCC and AppleClang was tried and removed again because it failed
  the CI builds; it is open until those builds are warning-free. The
  sanitizer entry builds since 63ba00fd4 (Clang links the shared sanitizer
  runtime on Linux) and its deviceless set passes locally with no report
  (1368 of 1368 with clang 18 and the CI options).

## Next

Items 13 and 14, selected 2026-10-05, worked together: the plan, its steps
and order are in `doc/plans/property_undo_and_reflective_mcp.md`. What else
is open is listed under "Open parts of worked items" and "Follow-ups found
by review".

## Open parts of worked items

- Item 18, Metal half: a persisted `MTLBinaryArchive` for the Metal backend
  (the `Device::warmup_render_pipeline` comment in `device.hpp` names the
  opt-in); needs a macOS session to build and verify.
- Item 6, Metal half: GPU timers (read 0), `blit_framebuffer` (fatal) and
  swapchain resize on Metal; needs a macOS session.
- Item 17, GPU tests in CI: the job runs the `gpu` label on lavapipe under
  the latest SDK's validation layer (`doc/testing.md` "CI"); its first run
  on a GitHub runner is outstanding. The goldens needed no per-driver
  tolerance locally. LeakSanitizer on the sanitizer entry
  (`ASAN_OPTIONS=detect_leaks=0` today) is open.

Each item is one commit with builds, tests and docs as `AGENTS.md` requires,
and each commit gets a Fable review at medium effort.

## Follow-ups found by review

`scripts/scene_roundtrip_verify.py` (run 2026-10-04 for item 11, against the
windowed Vulkan editor) fails three checks on 7c7c0c0bf; state and findings
in `doc/plans/scene_roundtrip_failures.md` ((a) fixed, (b) open with the
trigger isolated, (c) one fixed in the script, one open):
(a) `reload-diff: nodes identical` -
the dynamic bodies `P6 Box` / `P6 Sphere` keep falling between the snapshot
and the reload; the glTF leg does not pause physics before its snapshot the
way the USD leg does (`toggle_physics`); (b) `VK_ERROR_DEVICE_LOST` with a
`VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID` fault on the first frame after
the USD leg opens `authored.usda` (one point light with shadow) following
the glTF leg, on the draw-list path only (with `set_draw_lists_enabled`
false the run completes); (c) with draw lists off, two USD checks:
`textured round-trip diff: local_property_names` (the reloaded `Gridded`
material carries `base_color_texture_wrap_u` / `_v` locally) and
`references_override: the def below a carrier authored nothing`.

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
