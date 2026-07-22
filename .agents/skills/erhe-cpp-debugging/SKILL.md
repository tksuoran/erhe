---
name: erhe-cpp-debugging
description: Debug an erhe C++ crash, abort, VERIFY/ERHE_FATAL, thrown exception, hang or deadlock at the source level with a real callstack and live variable values, instead of guessing from logs/log.txt and a stripped backtrace. On macOS drive lldb from the Bash tool (preferred: the embedded Python SB API emitting JSON); on Windows drive the live Visual Studio via the visualstudio MCP server (breakpoints, launch-under-debugger, step, callstack, locals). Use this for "editor.exe exited non-zero / [crash] unhandled exception / a minidump appeared / a VERIFY or FATAL fired / a C++ exception 0xe06d7363 / it deadlocked or hung" -- the minidump and log are a starting hypothesis, the live callstack + locals are the diagnosis. For GPU "renders wrong" bugs use erhe-renderdoc-gpu-debug instead; this skill is for CPU-side control flow and state.
---

# erhe C++ debugging (lldb on macOS / Linux, Visual Studio MCP on Windows)

Reproduce the failure under a debugger and read the real callstack plus the live
variable values at the fault site. Do NOT try to reason a crash out from
`logs/log.txt` plus a stripped console backtrace alone, and do not just stare at a
`logs/editor_crash_*.dmp` minidump -- they are a hypothesis, not the diagnosis.

This is the authoritative macOS/Linux lldb run-book. The Windows VS-MCP prose
(full tool list, typical flow, gotchas) lives in CLAUDE.md under "Debugging on
Windows / MSVC (Visual Studio MCP server)".

## Pick the platform path

- **macOS / Linux -> lldb via the Bash tool.** `/usr/bin/lldb` ships with the
  Command Line Tools (no install). All three Debug builds have full symbols:
  `build_xcode_{metal,opengl,vulkan}/src/editor/Debug/editor`.
- **Windows -> the `visualstudio` MCP server** (`mcp__visualstudio__*`). It drives a
  live VS instance: `debugger_add_breakpoint` -> `debugger_launch` -> poll
  `debugger_status` until it breaks -> `debugger_get_callstack` /
  `debugger_get_locals` / `debugger_evaluate` -> `debugger_stop`. Always
  `solution_info` first to confirm which solution is loaded; for headless-only
  repros open `build_vs2026_vulkan_headless/erhe.slnx`. (CLAUDE.md has the full
  tool list and flow.) `lldb-mi`/GDB-MI do not exist here; on Windows use VS-MCP.

## macOS: which lldb interface (most efficient first)

1. **Python SB API emitting JSON -- DEFAULT for almost everything.** Write a script
   that drives the debugger and `print`s one JSON blob in a schema you control, then
   run it in one batch call and parse stdout. You fetch exactly the fields you need
   (minimal tokens), no protocol handshake, no scraping human-formatted `bt` text:

   ```bash
   lldb --batch -o "command script import /tmp/probe.py" -o "quit"
   ```
   `probe.py` defines `def __lldb_init_module(debugger, internal_dict):`, sets
   `debugger.SetAsync(False)`, does the work on the passed-in `debugger`, and
   delimits the JSON with sentinels (`print("JSON_BEGIN"+json.dumps(out)+"JSON_END")`)
   so you can extract it from lldb's own chatter. (`lldb -P` prints the SB-API module
   path if you want to `import lldb` from a matching python.)
2. **lldb-dap** (`xcrun -f lldb-dap`) only when you need a *persistent interactive*
   session across multiple tool calls (genuine step-debugging) -- run it backgrounded
   on a socket (`lldb-dap --connection listen://localhost:PORT`) and send DAP
   JSON-RPC requests (`launch`, `setBreakpoints`, `stackTrace`, `scopes`,
   `variables`, `evaluate`, async `stopped` events; Content-Length framing, seq
   correlation) so the debuggee stays stopped between inspections. More plumbing;
   not for one-shot captures.
3. **Plain CLI** (`lldb -b -o "..." -o "..."`) -- fine for a quick one-line peek.
4. lldb's built-in MCP server and GDB/MI are NOT worth it here. Inside lldb,
   `protocol-server start MCP listen://localhost:PORT` starts an `lldb-mcp` server,
   but it exposes exactly one tool (`lldb_command`, text back) over raw TCP --
   plumbing for no structured-data gain. Apple ships no `lldb-mi`.

## Canonical recipes (macOS, via the SB API / Bash)

- **Reproduce-and-capture a crash / VERIFY / FATAL / C++ exception** (macOS analog of
  the Windows VS-MCP "launch -> break on unhandled exception -> dump callstack +
  locals"): an SB-API script that creates the target, sets breakpoints (or relies on
  the stop at the fatal signal/throw), `Launch`es, and on stop walks `SBProcess` ->
  threads -> frames -> `SBValue` variables, emitting JSON. One call, structured result.
- **Snapshot a hung / deadlocked editor** (attach to the already-running pid -- a
  windowed launch is unaffected by attach): iterate every `SBThread`, dump each
  frame's function/file/line to JSON, then detach. Quick text form:
  `lldb -b -o "attach -p <pid>" -o "thread backtrace all" -o "detach" -o "quit"`.
  For deadlocks the root cause is usually on a *different* thread than the stuck one,
  so always dump ALL threads, not just the apparent victim.
- **Post-mortem**: `ulimit -c unlimited`, reproduce, then `lldb <editor> -c
  /cores/core.<pid>` (or `target create` + `target.LoadCore` in the SB API).

## Constraints

- **The windowed editor needs a live display.** Metal/OpenGL builds have no headless
  variant, so a *windowed* launch under lldb still aborts at startup if the display is
  off/asleep. Attaching to an already-running editor is unaffected. (On Windows the
  headless Vulkan build avoids this; on macOS there is no headless GPU build.)
- **Building & launching to verify is self-serve** -- you may build
  `build_xcode_metal`/etc. and launch the editor on your own initiative (mind the
  live-display requirement); only ask if the user told you to.
- Run erhe gtest suites serially, one failure at a time (see the run-tests-serially
  memory); an abort hides the rest of the run.

## When NOT this skill

- "Renders nothing / renders wrong / wrong color / which texture or uniform holds
  what" is a GPU question -> use **erhe-renderdoc-gpu-debug** (capture a frame, read
  render targets and shader constant buffers). This skill is CPU-side: control flow,
  crashes, hangs, and live C++ object state.
