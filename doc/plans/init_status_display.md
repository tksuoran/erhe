# Init status display -- Phase II (deferred)

## Status

Deferred. Phase I (`src/editor/init_status_display.{hpp,cpp}` plus the
`init_message` lambda wired into `Programs::load_programs`) is what
ships today. Phase I assumes the editor's init is single-threaded --
which it is: the `ERHE_SERIAL_INIT` / `ERHE_PARALLEL_INIT` toggle was
retired and init is serial by construction -- so no synchronization is
required between the worker(s) that publish status text and the main
thread that draws it.

This document captures the shape of the work that becomes necessary
if init ever goes parallel (on the GL worker-context API of
`doc/gl_worker_thread_contexts.md`).

## Goal

Let any worker thread call `init_message(msg)` while the main thread
pumps the on-screen status display. This restores parallelism between
shader compile, texture / buffer uploads, and other init work, and
generalizes the API so future long-running init steps can publish
status from any thread.

## Required graphics-layer changes

`Device::get_device_frame_command_buffer()` currently returns one
command buffer per device, recorded by whichever thread happens to
call it -- see for example
`src/erhe/graphics/erhe_graphics/vulkan/vulkan_device.cpp:1087, 1190`
and `src/erhe/graphics/erhe_graphics/metal/metal_device.cpp:419, 564,
692, 805`. The OpenGL backend has no command buffer per se but relies
on a single shared GL context.

- **Vulkan**: per-thread (or per-worker-context) primary or secondary
  command buffer tied to the active device-frame slot. `end_frame()`
  collects all per-thread CBs and submits them as one batch. The
  starting points are
  `src/erhe/graphics/erhe_graphics/vulkan/vulkan_device.hpp:269
  ensure_device_frame_command_buffer(size_t)` and the
  `m_active_device_frame_command_buffer` member at line 410.
- **Metal**: one `MTL::CommandBuffer` per worker thread tied to the
  device frame; all committed in `end_frame()`.
- **GL**: `ERHE_GET_GL_CONTEXT` (defined inline in `editor.cpp` around
  line 583) already gives each worker its own context. Add a thread-id
  assertion in the device-frame helpers so violations fail loudly.

The interleaving rule for `Init_status_display::pump()` must also be
reformulated so the main thread can present a swapchain frame while
workers are still recording into their own per-thread CBs. Two viable
shapes:

1. Pump runs only when no workers are inside a device-frame-recording
   call, gated by a coarse mutex around the device frame.
2. Pump opens a separate "status" device frame on a dedicated CB while
   the worker CBs continue recording into the init device frame.

## Required editor changes

- `Init_status_display::set_line(...)` adds a mutex; multiple workers
  may publish concurrently. `pump()` stays main-thread-only.
- Replace `m_executor->run(taskflow).wait()` at `editor.cpp:1405` with
  a polling loop:

  ```cpp
  tf::Future<void> fut = m_executor->run(taskflow);
  while (fut.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
      m_init_status_display->pump();
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  fut.get();
  m_init_status_display->pump();
  ```

- Each worker thread gets its own status line, so the user sees
  parallel activity rather than one flickering line. Two viable
  shapes:

  1. The single `init_message` lambda routes by
     `std::this_thread::get_id()` -> a worker-line slot allocated
     lazily by `Init_status_display`. Callers do not need to know
     their slot.
  2. The lambda becomes worker-aware: each worker is handed its own
     `Init_message_fn` that writes to a fixed pre-assigned line. This
     requires the editor to know the worker count up front.

  Option 1 is simpler and keeps the existing single-lambda API the
  editor already uses for serial init; the implementation should
  default to it unless a constraint forces option 2.

- `Programs::load_programs` keeps the same signature; if it later
  parallelizes the compile / link loops at
  `src/editor/renderers/programs.cpp:157-160, 168-170`, each parallel
  call writes to its worker's own line.

## Verification

- Build all three backends (OpenGL, Vulkan, Metal) and confirm the
  per-thread CB refactor compiles and submits cleanly.
- Smoke run on each backend: status text appears, shader compile and
  other init tasks run in parallel (overall init wall time roughly
  back to pre-Phase-I numbers), and runtime rendering after init is
  correct.
- Run `py -3 scripts/test_editor_mcp.py --no-physics --seed 42` on
  each backend.
- Inspect a captured init frame in RenderDoc / equivalent to confirm
  worker-recorded CBs and the main-thread status frames submit
  cleanly without stalling each other.

## Risks / open questions

- Whether the main thread can safely interleave a swapchain frame
  between worker submits depends on each backend's queue-submission
  ordering guarantees. Will need experimentation.
- If per-thread CBs are too costly on Vulkan, fall back to a "status
  quiet zone": briefly stop accepting new worker tasks for ~16 ms
  while the status frame submits.
