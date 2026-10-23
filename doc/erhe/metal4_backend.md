# Metal 4 Graphics Backend

Stability: experimental

The Metal 4 backend implements `erhe::graphics` on Apple's Metal 4 API (the
`MTL4*` classes, macOS 26 and later) beside the Metal 3 backend
(`doc/erhe/metal_backend.md`). It is built subsystem by subsystem; the plan,
its order and its verification are in `doc/plans/metal_backend.md` section 7
"Metal 4".

## Present state

The backend is the Metal 3 backend's code in its own source directory: every
subsystem still uses the Metal 3 API (see "Migration status"), so its
behavior, its images and its test results are those of the Metal 3 backend.
Everything `doc/erhe/metal_backend.md` describes (shader translation, the
buffer index layout, the texture heap, the swapchain, frame pacing, the
GPU-read draw count, GPU timers, deferred destruction, thread safety) applies
unchanged until the subsystem's row below says otherwise; this document
records only what differs.

The device logs `Metal 4 backend, device: <name>` at startup and reports
`Device_info::api_info` as `Metal 4 (<name>)`; the editor window title ends
in `Metal 4`.

## Build

```bash
scripts/configure_xcode_metal4.sh
cmake --build build_xcode_metal4 --target editor --config Debug
METAL_BACKEND=metal4 scripts/run_graphics_tests_metal.sh
```

`ERHE_GRAPHICS_API=metal4` (Apple platforms only) uses the same CPM packages
as `metal` (metal-cpp `metal-cpp_27`, which carries the `MTL4*` headers, and
the SPIRV-Cross fork).

## Defines

- `ERHE_GRAPHICS_API_METAL` names the Metal family. Both `metal` and `metal4`
  define it, so every site that only asks "is this Metal" (shader
  translation in `glsl_to_spirv.cpp` and `shader_stages_create_info.cpp`, the
  SDL window flag, the sky renderer, frame capture in `device.cpp`, the
  autorelease pools of `Scoped_transient_object_pool`) serves both backends.
- `ERHE_GRAPHICS_API_METAL4` is defined only by `metal4` and selects the Metal
  4 backend. Code that differs between the backends tests it first:

```cpp
#if defined(ERHE_GRAPHICS_API_METAL4)
# include "erhe_graphics/metal4/metal4_device.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_device.hpp"
#endif
```

## Source layout

`src/erhe/graphics/erhe_graphics/metal4/` holds one `metal4_<name>` file per
`metal/metal_<name>` file. The pimpl classes keep their names
(`Device_impl`, `Command_buffer_impl`, ...) because a build tree compiles
exactly one backend. Backend-neutral Apple code stays shared in
`erhe_graphics/` (`clock_monitor_apple.mm`, `thermal_state_apple.mm`), as does
the generated `Metal_config` (`definitions/metal_config.py`).
`metal4_pixel_format_table.cpp` is generated with the Metal 3 table by
`scripts/metal_format_tables/generate.py`.

The GPU tests compare against the same goldens as every backend
(`src/erhe/graphics/test/golden/`); `results.json` names the backend
`metal4`.

## Migration status

One row per subsystem, in the order the plan ports them. "Metal 3 code" means
the subsystem is the unchanged copy of the Metal 3 backend.

| Subsystem | State |
| :--- | :--- |
| Device, `MTL4CommandQueue`, `MTL4CommandBuffer`, `MTL4CommandAllocator`, `MTL4CommitFeedback` | Metal 3 code |
| Argument tables (`MTL4ArgumentTable`), residency sets (`MTLResidencySet`) | Metal 3 code |
| Render, compute and blit encoders, barriers | Metal 3 code |
| Swapchain, drawable, frame pacing | Metal 3 code |
| Counter heaps (GPU timers, Tracy GPU zones) | Metal 3 code |
| `MTL4Compiler` (shaders, pipelines) | Metal 3 code |
| GPU-read draw count (`executeCommandsInBuffer`) | Metal 3 code |

## Future work

The ports in the table above, each gated by the Metal ctest suite and the
image, race and pacing gates (`doc/plans/metal_backend.md` section 7).
