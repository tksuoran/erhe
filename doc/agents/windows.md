# Windows sessions

Stability: stable

What an agent session on Windows needs beyond `AGENTS.md`: the build trees and
the edit-build loop, the clangd database, and shell hygiene. Debugging is in
`doc/agents/debugging.md`; running and driving the editor in
`doc/agents/editor_runs.md`.

## Build trees

Always configure through the `scripts\` wrappers; they encode the project's
configure flow (CPM caching, MSVC environment init, the options they pass).
The full list of wrappers and CMake options is in `doc/building.md`.

| Tree | Configure | Build | Use |
|------|-----------|-------|-----|
| `build_ninja_win_vulkan/` | `scripts\configure_ninja_win_vulkan.bat` | `scripts\build_ninja_win_vulkan.bat editor` | Day-to-day edit-build loop (MSVC `cl`) |
| `build_ninja_win_clang/` | `scripts\configure_ninja_win_clang.bat` | `scripts\build_ninja_win_clang.bat editor` | clang-cl; its configure regenerates `compile_commands.json` for clangd |
| `build_vs2026_vulkan_headless/` | `scripts\configure_vs2026_vulkan_headless.bat` | `cmake --build build_vs2026_vulkan_headless --target editor --config Debug` | Headless editor for MCP-driven verification without a display; tests ON |
| `build_vs2026_vulkan/` | `scripts\configure_vs2026_vulkan.bat` | Visual Studio solution, or `cmake --build ... --config Debug` | Windowed build the user runs; tests ON |

The ninja wrappers locate VS 2026's bundled cmake/ninja (via `vswhere`) and
set up the MSVC environment themselves, so they work from any shell. The VS
wrappers are meant for an x64 Native Tools Command Prompt. Other VS variants:
`configure_vs2026_vulkan_asan.bat`, `configure_vs2026_opengl.bat`,
`configure_vs2026_opengl_asan.bat`, `configure_vs2026_opengl_no_tracy.bat`.

Every executable and shared library of a build tree is written to
`<build>/bin/` (`<build>/bin/<Config>/` under the Visual Studio generator),
e.g. `build_vs2026_vulkan_headless/bin/Debug/editor.exe`,
`build_ninja_win_vulkan/bin/editor.exe`.

Kill `editor.exe` before every relink: a running editor holds the executable
and the link fails with LNK1168.

A configure while the solution is open in Visual Studio can pop a modal dialog
that blocks the Visual Studio MCP server; tell the user before configuring and
ask them to clear any dialog that appears.

## clangd diagnostics vs the real build

IDE diagnostics come from clangd via `build_ninja_win_clang/compile_commands.json`.
That build tree is not part of the routine build loop, so nothing keeps its
database fresh on its own.

**After any change that affects compile commands -- editing a
`CMakeLists.txt`, adding/removing/renaming source files, or adding a new
`erhe::*` library / include directory -- re-run
`scripts\configure_ninja_win_clang.bat`.** It is configure-only (~25 s, no
compilation) and regenerates the database. The IDE's clangd may need a
language-server restart to pick it up.

Staleness signature: ONE unresolved include (a header the stale database has
no include path for) cascades into hundreds of false errors in perfectly
compiling, committed code -- "file not found", then "unknown type", "no member
named ... in <well-known class>", "cannot initialize object parameter" across
unrelated files. Do not chase these -- the ninja/VS build is the ground truth;
refresh the database instead. (Precedent: a database predating
`src/erhe/frame_pacing` poisoned diagnostics repo-wide via the single missing
`erhe_frame_pacing/frame_time_recorder.hpp` include path.) To verify a refresh
worked without an IDE: `clangd --check=<file>` -- only `tweak:` probe failures
in its output means the file is clean.

## Shell process hygiene (find.exe)

`find` resolves to Git Bash's `C:\Program Files\Git\usr\bin\find.exe`. A broad
search such as `find / -name <file>` walks the entire Windows filesystem and
effectively hangs, leaving an orphaned `find.exe` running in the background.
Use the Glob/Grep tools, or scope `find` to a specific directory. After using
`find`, check for leftover `find.exe` processes and kill any that remain:

```powershell
Get-CimInstance Win32_Process -Filter "Name='find.exe'" | Select-Object ProcessId, CommandLine | Format-List
Get-CimInstance Win32_Process -Filter "Name='find.exe'" | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
```

PowerShell 5.1 mangles inline JSON containing spaces when passing it to native
executables; pass MCP arguments to `scripts/mcp_call.py` in its `b64:` form or
on stdin (`doc/agents/editor_runs.md`).
