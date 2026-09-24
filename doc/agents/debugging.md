# Debugging

Stability: stable

How an agent session diagnoses crashes, hangs and GPU bugs. Skills:
**`erhe-cpp-debugging`** for any C++ crash / abort / `VERIFY` / `ERHE_FATAL` /
thrown exception / hang (Windows Visual Studio MCP plus the macOS/Linux lldb
run-book); **`erhe-renderdoc-gpu-debug`** for "renders wrong" GPU bugs.

## Live callstack first

Reproduce a crash **live under a debugger** and read the real callstack plus
the locals at the fault site. The minidump (`logs/editor_crash_*.dmp`) and
`logs/log.txt` are a starting hypothesis, not the diagnosis. After any crashed
or aborted AI-driven run, grep `logs/log.txt` and read `logs/device_error.txt`
/ `logs/shader_error.txt` (`doc/agents/editor_runs.md`) before theorizing.

Multithreading: when diagnosing deadlocks or contention, get the callstacks of
all threads - not just the stuck thread. The root cause is usually on another
thread.

## Windows: Visual Studio MCP server

Use the **`visualstudio` MCP server**
([CodingWithCalvin/VS-MCPServer](https://github.com/CodingWithCalvin/VS-MCPServer))
for interactive debugging and build/diagnostic queries. It drives a *live*
Visual Studio instance: set breakpoints, launch under the debugger, step, and
inspect program state.

**Prerequisites (one-time, done by the user):**
- Visual Studio 2022 or 2026 with the "MCP Server" extension installed
  (Extensions > Manage Extensions, search "MCP Server"; requires the .NET 10
  SDK).
- The server started inside VS: Tools > MCP Server > Start Server (listens on
  `http://localhost:5050`; can be set to auto-start in Tools > Options > MCP
  Server).
- The erhe solution open in that VS instance (e.g.
  `build_vs2026_vulkan/erhe.slnx`). The tools act on whatever solution is
  currently loaded -- always call `mcp__visualstudio__solution_info` first to
  confirm which one.
- Permission: configure the active AI client to allow the `mcp__visualstudio`
  server without per-call prompts.

**Tool groups** (all prefixed `mcp__visualstudio__`):
- Solution/project: `solution_info`, `solution_open/close`, `project_list`,
  `project_info`, `startup_project_get/set`.
- Build: `build_solution`, `build_project`, `build_status` (poll until
  `State == "Done"`, check `FailedProjects`), `build_cancel`, `clean_solution`.
- Diagnostics: `errors_list` (severity / code / file / line), `output_read`,
  `output_list_panes`.
- Debugger: `debugger_add_breakpoint` / `list_breakpoints` /
  `remove_breakpoint`, `debugger_launch` (with debugging) /
  `launch_without_debugging`, `debugger_status` (`Mode` is `Design` when not
  running), `debugger_break` / `continue` / `stop`,
  `debugger_step_into/over/out`, `debugger_get_callstack`,
  `debugger_get_locals`, `debugger_evaluate`, `debugger_set_variable`.
- Documents/editor/navigation: `document_open/read/write/save`,
  `editor_find/replace`, `editor_goto_line`, `goto_definition`,
  `find_references`, `selection_get/set`.

**Crash repro flow:**
1. `solution_info` to see which solution is loaded. The crash is almost always
   in shared `editor` / `erhe::*` code, so it reproduces on **any** build's
   `editor` target -- prefer the solution already open. To debug the
   **headless** build specifically (e.g. a crash only seen while driving the
   in-editor MCP), `solution_open` `build_vs2026_vulkan_headless/erhe.slnx`;
   for windowed use `build_vs2026_vulkan/erhe.slnx` (needs a live display) or
   `build_vs2026_opengl/erhe.slnx`.
2. If the loaded `editor` binary predates your change, `build_project` the
   `editor.vcxproj` and poll `build_status` until `State=="Done"` /
   `FailedProjects==0`.
3. `debugger_add_breakpoint` at the suspected throw site(s) if you have a
   hypothesis; otherwise `debugger_launch` and let the debugger break on the
   unhandled exception (the editor's crash handler runs at second chance, so
   the user-stack throw frame is still visible).
4. Drive the repro (e.g. the in-editor MCP `tools/call` over HTTP, or the
   input that triggers it), poll `debugger_status` until `Mode != "Design"`,
   then `debugger_get_callstack` + `debugger_get_locals` /
   `debugger_evaluate` to read the faulting state. `debugger_stop` when done.

**Notes / gotchas:**
- `symbol_workspace` does not index this C++ solution's symbols (it is
  C#-oriented). For C++ symbol navigation use `goto_definition` /
  `find_references` from an open document, or Grep over `src/`.
- `debugger_get_callstack` reads the current thread only; for a multi-thread
  hang ask the user to switch threads in the Threads window between reads.
- There is no attach tool (`debugger_launch` only). For a hung process that
  was started outside VS, ask the user to attach manually (Debug > Attach),
  then `debugger_break` / `debugger_get_callstack`. A non-invasive
  alternative for an all-thread snapshot is `cdbX64 -p <pid> -pv -c "~*k"`
  when the WinDbg package is installed.
- The startup project is `editor`; `build_status` reports
  `NoBuildPerformed` until a build is triggered through VS or the MCP build
  tools.
- **Modal dialogs in VS block the MCP server.** A CMake configure while the
  solution is open can pop a modal dialog the server cannot dismiss; tool
  calls hang until a human clears it. Tell the user beforehand.
- If the `mcp__visualstudio__*` tools are not registered in the session, the
  server is still reachable over plain HTTP on `http://localhost:5050`
  (JSON-RPC 2.0) -- probe it before concluding it is unavailable.

## macOS / Linux: lldb

The **`erhe-cpp-debugging`** skill is the complete lldb run-book. There is no
Visual Studio MCP equivalent on macOS, and the Xcode MCP server has no
debugger control.

## Missing tooling: ask, don't settle

When a debugging or analysis task would be materially easier with software
that is not installed (a post-mortem debugger such as WinDbg/cdb, a profiler,
a dump or trace analyzer), tell the user what is missing, what it would buy
for the task at hand, and how to install it (command or link), and ask them to
install it or for permission to install it yourself. Record newly installed
per-machine tool locations in `memory-bank/local/`. Check this document first:
the capability may already exist through an MCP server (Visual Studio MCP for
live debugging, the RenderDoc fork for GPU captures). Precedent: a minidump
was "analyzed" with a hand-rolled dbghelp stack walker that produced a single
usable frame, when the Visual Studio MCP flow (or WinDbg,
`winget install Microsoft.WinDbg`) would have given the full callstack and
locals.

## GPU frame debugging (RenderDoc fork)

For windowed Vulkan GPU debugging -- pipeline state, render targets, texture
statistics, pixel history, constant-buffer contents, post-VS geometry -- use
the RenderDoc fork MCP server through the **`erhe-renderdoc-gpu-debug`**
skill. It needs a live display and a windowed Vulkan build (not Metal, not
OpenGL, not the headless emulated swapchain). Setup (the stdio proxy
`scripts/renderdoc_mcp_proxy.py`, `scripts/setup_renderdoc_mcp.py`, the tool
schema `scripts/renderdoc_tools.json`) and the worked examples are in
`doc/agents/renderdoc_fork.md`. For just seeing the final frame, the
in-editor MCP `capture_screenshot` is enough.

## Profiling

Startup and frame profiling with Tracy (`ERHE_TRACY_ON_DEMAND`,
`scripts/tracy_startup_profile.py`) is described in `doc/building.md`.
