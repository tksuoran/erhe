---
name: erhe-macos-xcode
description: Build erhe and read compiler / project diagnostics on macOS through Apple's official Xcode MCP server (mcp__xcode__* tools) driving the live Xcode instance that has the erhe .xcodeproj open. Use for any macOS build, build-error triage, Issue-Navigator query, per-file compiler diagnostics, in-project file navigation, or Apple-documentation (Metal API) lookup. It is NOT a debugger and cannot launch or drive the editor -- no breakpoints / attach / step / callstacks (use erhe-cpp-debugging's lldb path), no scheme or run-destination selection, no way to run the app (use the in-editor MCP server / headless flows). If no Xcode window is open, fall back to scripts/configure_xcode_*.sh + cmake --build.
---

# erhe macOS builds / diagnostics (Xcode MCP server)

Prefer the `xcode` MCP server (Apple's official Xcode MCP, prefix `mcp__xcode__`)
for build, diagnostics, and in-project file navigation over ad-hoc shell
`xcodebuild` invocations. It drives a *live* Xcode instance that has the erhe
`.xcodeproj` open, so it builds the exact scheme/config the user is working in
and returns structured errors. Verified working against
`build_xcode_vulkan/erhe.xcodeproj` (the Xcode-generated project is one logical
`erhe` tree; C++ sources are reachable, e.g.
`erhe/erhe-executables/editor/main.cpp`).

**Always call `mcp__xcode__XcodeListWindows` first** to get the `tabIdentifier`
(e.g. `windowtab1`) and confirm which workspace is loaded -- every other tool
requires that `tabIdentifier`. If no Xcode window is open, fall back to the
`scripts/configure_xcode_*.sh` + `cmake --build` flow.

## What it CAN do (use these)

- **Build + diagnostics**: `BuildProject` (builds the active scheme, waits for
  completion), `GetBuildLog` (errors/warnings/remarks, filterable by
  `severity` / `pattern` regex / `glob`; reports `buildIsRunning` and
  `buildResult`), `XcodeListNavigatorIssues` (live Issue-Navigator issues incl.
  package/config problems -- can be huge, filter by `severity`/`glob`/`pattern`),
  `XcodeRefreshCodeIssuesInFile` (per-file compiler diagnostics).
- **Project navigation / editing within the Xcode project organization** (NOT
  filesystem paths): `XcodeLS`, `XcodeGlob`, `XcodeGrep` (regex; remember
  double-escaping in the JSON arg, e.g. `\\d`), `XcodeRead`, `XcodeWrite`,
  `XcodeUpdate`, `XcodeMV`, `XcodeRM`, `XcodeMakeDir`, `XcodeGetCurrentFile`.
  For ordinary editing of erhe sources the regular Read/Edit/Write/Grep/Glob
  tools on real filesystem paths are usually simpler; reach for the `Xcode*`
  file tools when you specifically need project-organization semantics (e.g.
  adding a file to the project).
- **Apple docs**: `DocumentationSearch` (semantic search of Apple Developer
  Documentation; useful for Metal API questions).

## What it CANNOT do (do NOT expect VS-MCP-style live debugging)

- **No debugger control whatsoever** -- there is no launch-under-debugger,
  breakpoints, step, continue, attach, callstack, or locals/variable
  inspection. This is the key difference from the Windows Visual Studio MCP
  server. To debug an erhe crash on macOS you need lldb directly (see the
  `erhe-cpp-debugging` skill), plus `logs/log.txt`.
- **No scheme / run-destination / launch-target selection** -- all
  build/test/run tools act on whatever scheme is *currently active* in the
  Xcode UI (here: `editor`). You cannot switch scheme or run target via MCP;
  ask the user to set it in Xcode.
- **No way to launch and drive the running editor app.** The only "run" tools
  are `RunAllTests` / `RunSomeTests` (active scheme's XCTest test plan --
  erhe's `editor` scheme has 0 XCTest tests, so not useful here),
  `ExecuteSnippet` (builds & runs a *Swift* snippet and returns its `print`
  output -- N/A to a C++ project), and `RenderPreview` (SwiftUI preview
  snapshots -- N/A). To actually run/inspect the editor, use the build scripts
  + the in-editor MCP server / headless screenshot flows.

**Bottom line:** on macOS the Xcode MCP server is the preferred tool for
*building and reading diagnostics* (and Apple-docs lookup), but it is NOT a
debugger and cannot launch or drive the editor. For runtime behavior use
`logs/log.txt` and the in-editor MCP server; for live debugging use lldb via
the `erhe-cpp-debugging` skill.
