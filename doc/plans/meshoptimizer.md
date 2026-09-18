# Mesh optimization: outstanding work

Status: proposed

Extends `doc/erhe/meshoptimizer_integration.md` and
`doc/erhe/meshoptimizer_attribute_encodings.md`.

## Measure the runtime win

The static side is measured (vertex-fetch bytes, weld ratios, stride). Frame
time is not: it is not measurable headlessly, because the frame pacer reports
tier "OFF" and no MCP surface reports GPU frame time. Use the Frame Pacing
window or a GPU capture on a RELEASE build.

Also measure:

- the transient RSS spike of the staging snapshot plus optimizer temporaries
  on a Bistro-scale rebuild with several finalize workers;
- the base variant's own memory (12 bytes per position, always resident);
- on Quest, whether the reduced vertex fetch shows up in frame time, which is
  where it should matter most on tiler hardware.

## Shader-compile stutter watch

The base and optimized formats carry different position encodings, so both
sets of content-shader variants exist whenever both variants render (the load
window, and any mesh whose variant an edit dropped). No stutter has been
observed in practice. The nearest measurement is Quest startup, where the
shader prewarm is the largest single phase - that is prewarm, not stutter, but
it is the same variant explosion. Keep watching, and reduce the variant set if
it shows.

## Quest verification of the attribute encodings

The compact attribute encodings are neither built nor tested on Quest. Two
things to settle when it happens:

- whether every substituted format is accepted as vertex input on the device
  (`format_16_vec4_sint` and `format_16_vec3_unorm` are the new ones; position
  quantization already has a `Device_info` gate and a declined-request warning
  to model a fallback on);
- whether the reduced vertex fetch shows up in frame time.

The config is APK-bundled, so a config change needs an uninstall and a clean
reinstall, and every OpenXR launch needs a fresh prompt and explicit
confirmation.

## Encoding candidates

- YCoCg vertex colors via `meshopt_encodeFilterColor` /
  `meshopt_decodeFilterColor`: quality at the same 4 bytes.
- `format_packed1010102_vec4_snorm` is declared but
  `erhe::dataformat::convert()` handles it neither as source nor destination.
  It is the classic 4-byte normal format; a future encoding wanting it has to
  close that gap first.
- Stream 2 of the optimized format holds `aniso_control` alone, 2 bytes padded
  to a 4-byte stride. Folding it into stream 1 removes a whole pool and its
  allocation, but the optimized and content formats would then differ in
  stream *count*, which `take_optimizable_snapshot()` rejects outright.
- 8-bit variants (`8_vec4_snorm` tangents, `8_vec3_snorm` normals) as a low-end
  profile, if Quest ever wants to trade precision for a few more bytes.

## Seams left open deliberately

Recorded so the shape is known, not scheduled:

- LOD chains (`meshopt_simplify`), a natural fit on the deferred-allocation
  staging seam, and meshlets.
- A cache size cap / LRU for `cache/mesh_optimizer/`; unbounded growth is
  accepted today.
- A dedicated minimal `id_renderer` variant (position + facet id +
  joints / weights) that would let the full original variant be dropped and
  reclaim the accepted 2x GPU mesh memory.
- A disk cache on the geometry path; extend the cache there only if profiling
  disagrees with the current reading that the passes are fast relative to the
  geometry build itself.
- An `ERHE_VERIFY(isfinite)` on the centroid position path: deliberately not
  added, because it would turn previously-silent broken scenes into aborts.
