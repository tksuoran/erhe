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
