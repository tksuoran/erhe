# 2026-09-30 audit: medium-term set and follow-ups

Status: in progress

The 2026-09-30 audit lives on branch `origin/audit-2026-09-30`
(`audit_erhe_2026_09_30.md` under `reference/` on that branch, section 8;
slice reports beside it; list with `git ls-tree -r --name-only
origin/audit-2026-09-30 | grep audit_erhe_2026_09_30` and read with
`git show origin/audit-2026-09-30:<path>`). Near-term items 1-9
are done. From the medium-term list (items 10-20) the chosen set is 10, 18 and
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

1. Item 18, Metal half: a persisted `MTLBinaryArchive` for the Metal backend
   (the `Device::warmup_render_pipeline` comment in `device.hpp` names the
   opt-in); needs a macOS session to build and verify.

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
