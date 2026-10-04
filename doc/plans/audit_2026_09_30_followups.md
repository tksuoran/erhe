# 2026-09-30 audit: medium-term set and follow-ups

Status: in progress

The 2026-09-30 audit lives on branch `origin/audit-2026-09-30`
(`audit_erhe_2026_09_30.md` under `reference/` on that branch, section 8;
slice reports beside it; list with `git ls-tree -r --name-only
origin/audit-2026-09-30 | grep audit_erhe_2026_09_30` and read with
`git show origin/audit-2026-09-30:<path>`). Near-term items 1-9
are done. From the medium-term list (items 10-20) the chosen set is 10, 18 and
the include-diet half of 12: item 10 closes the only known GPU correctness hole
and was an open agfx-port finding, item 18 has a ready plan
(`doc/plans/spirv_cache.md`) and dominates Quest first-frame time, and the
include diet is the precondition of the `erhe_graphics` interface / backend
split. Items 11 and 13-16 are multi-week redesigns, 17 is CI work, 19-20 wait
on culling.

## Done

- Item 10: per-subresource Vulkan layout tracking (4fc17d988) and the
  `Texture_location` / `Buffer_texel_location` blit region types (1d5d45f28,
  narrowing fix 51dfc09fb). The OpenGL failures found while testing it are in
  [`opengl_test_failures.md`](opengl_test_failures.md).

## Next

1. Item 18, shader pipeline persistence: `doc/plans/spirv_cache.md` (settings
   hash in the salt, atomic rename), then a persisted `VkPipelineCache`
   (`device.hpp` pipeline cache hooks) and a Metal `MTLBinaryArchive`.
2. Item 12, first half: include diet on `erhe_graphics/device.hpp` and
   `texture.hpp` (`texture.hpp` pulls `erhe_item/item.hpp` and `typed.hpp`, and
   with them the property system; `enums.hpp` is in every graphics header).
   The interface / backend CMake split follows only if the diet leaves it
   clean.

Each item is one commit with builds, tests and docs as `AGENTS.md` requires,
and each commit gets a Fable review at medium effort.

## Follow-ups found by review

Latent defects of the layout tracking (4fc17d988), none reachable by a
current caller; fix each with a test that reaches it:

- `Command_buffer_impl::clear_texture` (`vulkan_command_buffer.cpp`) clears
  `0 .. VK_REMAINING` of the whole `VkImage` but transitions only the
  texture's own range (`get_all_subresources()`), so clearing a texture view
  with a non-zero base level / layer touches subresources not in
  `TRANSFER_DST_OPTIMAL` and leaves their tracked layout stale. Build the
  clear range from `get_all_subresources()` plus the view base.
- Stencil-only multisample resolve (`vulkan_render_pass.cpp`): the
  constructor resolves through `m_stencil_attachment.resolve_texture` when
  depth has none, but `end_render_pass` records the resolve target's
  `finalLayout` only for a depth resolve, and the resolve attachment's
  `finalLayout` reads the depth attachment's `layout_after`. Remember which
  attachment drove the resolve and record its range and final layout.
- `generate_mipmaps` (`vulkan_blit_command_encoder.cpp`) takes the blit
  extents from `get_width()` / `get_height()`, which for a texture view are the
  source's level-0 size (`Texture_create_info::make_view` TODO), while the
  regions add the view's base level. Derive the extents from the image level.

Dead code: `load_texture()` in `src/hextiles/texture_util.cpp` has no callers
and copies only a 2x2 extent of the loaded image; delete it.

`Gpu_test.device_up_clean` fails on machines whose Vulkan validation layer is
older than the pinned Vulkan headers (an unknown `sType` in the device create
`pNext` chain); that is a machine setup issue (`doc/agents/linux.md`), not a
code defect.
