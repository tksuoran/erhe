# macOS sessions

Stability: stable

What an agent session on macOS needs beyond `AGENTS.md`.

## Build trees

The OpenGL backend is not supported on Apple platforms; use Metal or Vulkan.
Configure through the `scripts/` wrappers (they enable tests by default):

```bash
scripts/configure_xcode_metal.sh           # Metal backend  -> build_xcode_metal/
scripts/configure_xcode_vulkan.sh          # Vulkan backend -> build_xcode_vulkan/
scripts/configure_xcode_metal_headless.sh  # headless Metal -> build_xcode_metal_headless/
```

Then build with:

```bash
cmake --build build_xcode_metal  --target editor --config Debug
cmake --build build_xcode_vulkan --target editor --config Debug
```

Executables land in `<build>/bin/<Config>/`. The headless Metal build is
described in `doc/erhe/metal_headless.md`.

## Building and diagnostics through Xcode

Invoke the **`erhe-macos-xcode`** skill for building and reading diagnostics
through Apple's Xcode MCP server (`mcp__xcode__*`; always call
`XcodeListWindows` first for the `tabIdentifier`; `BuildProject` +
`GetBuildLog` for structured errors; Apple-docs search). It is not a debugger,
cannot select schemes or run destinations, and cannot launch or drive the
editor. When no Xcode window is open, build with the wrappers above.

## Debugging

Invoke the **`erhe-cpp-debugging`** skill: it is the lldb run-book (Python SB
API emitting JSON preferred; crash capture, all-thread hang snapshots,
post-mortem cores). The windowed editor needs a live, awake display
(attaching to an already-running editor is unaffected). Building and launching
the editor to verify a change is self-serve.

For GPU bugs seen on the Metal build, reproduce them on `build_xcode_vulkan`
and use the **`erhe-renderdoc-gpu-debug`** skill (RenderDoc cannot capture
Metal).
