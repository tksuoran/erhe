# Linux sessions

Stability: stable

What an agent session on Linux needs beyond `AGENTS.md`.

## Build trees

Vulkan is the default backend:

```bash
scripts/configure_ninja_linux_vulkan.sh
cmake --build build_ninja_linux_vulkan --target editor
```

For the OpenGL backend, use `scripts/configure_ninja_linux_opengl.sh` and
build `build_ninja_linux`. Required packages are listed in `doc/building.md`.
Executables land in `<build>/bin/`. Debugging with lldb: the
**`erhe-cpp-debugging`** skill.

## Headless runs and GPU tests

`scripts/configure_ninja_linux_vulkan_headless.sh -DERHE_BUILD_TESTS=ON`
configures `build_ninja_linux_vulkan_headless` (no window library). Its
editor and the `*_gpu_tests` executables need no display: a locked or absent
session does not stall them. Select the Vulkan driver explicitly and keep
implicit layers (overlays, capture tools) out of the run:

```bash
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/<driver>_icd.json \
VK_LOADER_LAYERS_DISABLE='~implicit~' \
build_ninja_linux_vulkan_headless/bin/erhe_graphics_gpu_tests
```

The GPU tests enable the Khronos validation layer and fail on any validation
error, so the layer must know every structure erhe chains. erhe builds
against the Vulkan headers it pins (`vulkan-headers` in `CMakeLists.txt`),
which are newer than the validation layer of most distribution packages; an
older layer reports `VUID-VkDeviceCreateInfo-pNext-pNext` ("unknown
VkStructureType") at device creation and `Gpu_test.device_up_clean` fails.
Use a validation layer from a Vulkan SDK at least as new as the pinned
headers (`VK_ADD_LAYER_PATH=<sdk>/share/vulkan/explicit_layer.d`).

Kill a stray editor with `pkill -x editor`; `pkill -f` patterns match the
shell running the command as well.

## AddressSanitizer

`-DERHE_USE_ASAN=ON` adds `-fsanitize=address` to every target of the tree
(`cmake/GNU.cmake`, `cmake/Clang.cmake`). Configure a separate tree with
cmake directly, passing the flags of the matching `scripts/configure_*.sh`
plus the option and `-DERHE_BUILD_TESTS=ON`, for example
`build/Ninja_OpenGL_Asan` with `-DERHE_GRAPHICS_API=opengl`. Linking needs
the compiler's sanitizer runtime: with Ubuntu's clang that is the
`libclang-rt-<version>-dev` package (`ld: cannot find
libclang_rt.asan-x86_64.a` says it is missing). Run with
`ASAN_OPTIONS=detect_leaks=0` unless leaks are the question. Without
`llvm-symbolizer` on the PATH the report prints raw `<exe>+0x...` offsets;
feed them to `addr2line -C -f -i -e <tree>/bin/<exe>` to get the symbolized
stacks.

## Memory growth diagnostics

Two zero-install tools. Both launch the editor in an isolated working
directory (copy of `config/` with optional overrides applied, `res/`
symlinked, shared spirv cache), so the user's `config/` is never touched and
runs are reproducible:

- `python3 scripts/idle_memory_check.py --duration 300` -- launches the
  editor, samples VmRSS / smaps_rollup / per-process DRM fdinfo GPU counters
  (GTT/VRAM) once a second, fits Theil-Sen + least-squares slopes, prints
  per-mapping growth attribution and a leak verdict (exit code 1 = growth).
  `--set 'file.json:dotted.path=value'` overrides any value in the copied
  config (works for logger levels in logging.json too); `--build-dir` selects
  other builds, including the OpenGL one.
- `python3 scripts/vk_alloc_census.py` -- runs the editor under gdb
  (ptrace_scope=1 forbids attaching, so gdb must be the parent) and counts DRM
  `GEM_CREATE` ioctls with requested sizes and backtraces, Vulkan loader
  create/destroy entry points, and glibc brk/anonymous-mmap heap growth, all
  normalized per `vkQueuePresentKHR` frame. This sees GPU allocations the
  driver makes internally (command-stream / descriptor upload BOs) that never
  go through `vkAllocateMemory`/VMA.

Steady growth needs over-time attribution, not exit-time leak checks:
teardown frees pool-owned objects, so a VMA dump or LSAN at exit can look
clean while every frame leaks. Precedent: a `VkCommandBuffer` allocated every
frame and only ever reset by `vkResetCommandPool` (which resets but never
frees pool-owned handles) grew the host heap and GTT without bound until
`Per_thread_command_pool` started reusing handles across frame-in-flight
cycles.
