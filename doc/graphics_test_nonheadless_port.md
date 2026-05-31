# Running erhe_graphics_gpu_tests on non-headless OpenGL / Metal

## Status

- The `erhe_graphics_gpu_tests` target now BUILDS on non-headless OpenGL and Metal
  (the CMake gate in `src/erhe/graphics/test/CMakeLists.txt` was widened: Vulkan with
  any window library, or OpenGL / Metal with a real window library).
- Headless Vulkan: **41/41 green** (unchanged).
- OpenGL (and presumably Metal): the target builds and the device comes up, but the
  suite does **not** yet run green. Runtime enablement is deferred; the concrete
  blockers are below. Do not assume a green GL run until these are fixed.

## What already works on OpenGL

- Device bring-up: a hidden (`show=false`) SDL window + GL context + `erhe::graphics::Device`
  via the same `Surface_create_info` path the editor uses. `Gpu_test.device_up_clean`
  passes on `build_vs2026_opengl`.
- Shader compilation: test shaders now use the portable `gl_VertexID` / `gl_InstanceID`
  convention (erhe remaps these to `gl_VertexIndex` / `gl_InstanceIndex` for Vulkan/Metal;
  OpenGL uses them natively). Previously the tests used Vulkan's `gl_VertexIndex` directly,
  which does not compile on the GL backend.

## Blockers to a green OpenGL run

### 1. Buffer readback / host-buffer map pattern (fixture)

`Gpu_test::read_buffer` / `make_host_buffer` (`src/erhe/graphics/test/gpu_test_fixture.cpp`)
create a host-visible buffer with `host_read | host_write | host_coherent` and then do a
transient `map_bytes` -> memcpy -> `unmap` per access. That pattern is contradictory on GL:

- A coherent mapping legally requires `MAP_PERSISTENT_BIT` -- `gl_buffer.cpp:96`
  (`get_gl_storage_mask`) aborts via `ERHE_VERIFY(persistent)` when coherent is requested
  without persistent.
- Making it persistent (so the VERIFY passes) causes the buffer to be mapped once at
  creation (`gl_buffer.cpp:293-296`), after which the fixture's explicit `map_bytes` trips
  `ERHE_VERIFY(m_map.empty())` at `gl_buffer.cpp:495`.

erhe's own `ring_buffer` (`src/erhe/graphics/erhe_graphics/ring_buffer.cpp:29-41`) avoids
this: it puts `host_read`/`host_write` in the *required* mask and `host_persistent`
(+ `host_coherent` / `host_cached`) in the *preferred* mask, maps persistently once, keeps
the pointer, and flushes/invalidates explicitly for the non-coherent case.

**Fix direction:** rework the fixture's buffer helpers to mirror the proven persistent-map
pattern (map once, expose the persistent span instead of re-mapping; this likely needs a
`Buffer` accessor for the existing mapping), keeping the Vulkan path green. Avoid the
band-aid of just toggling memory-property flags -- that only moves the abort.

### 2. Render-path silent abort

`Gpu_test.blend_premultiplied_over` (and likely every render test) aborts with exit code 3
and **no callstack and no log output** -- a `std::terminate`, not an `ERHE_VERIFY` (those
print a callstack, as the buffer abort above does) and not a GL-backend `throw` (there are
none in `src/erhe/graphics/erhe_graphics/gl/`). It must be diagnosed with a debugger; `cdb`
is not installed on the dev machine. Run e.g. under the Visual Studio debugger or install
the Windows SDK Debugging Tools, break on the abort/terminate, and capture the stack:

```
cdb -c "sxe -c \"kb 40;q\" eh; sxe -c \"kb 40;q\" 80000003; g" \
    build_vs2026_opengl\src\erhe\graphics\test\Debug\erhe_graphics_gpu_tests.exe \
    --gtest_filter=Gpu_test.blend_premultiplied_over
```

## Metal

The gate enables the Metal build, but Metal cannot be built or run on the Windows dev
machine. It needs a macOS (Xcode) build + run to validate; the same two blocker classes
(buffer map pattern, render path) should be re-checked there.

## Bisection snapshot (OpenGL, build_vs2026_opengl)

`device_up_clean` PASS; every test that creates a host/readback buffer or renders ABORTs
(exit 3). Buffer-path aborts show an `ERHE_VERIFY` callstack; render-path aborts are silent.
