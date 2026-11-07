# Shader analysis: verification on other platforms and open items

Status: proposed

Extends `doc/editor/shader_analytics.md` ("Session programs") and
`doc/erhe/graphics.md` ("SPIR-V cache records", "PowerVR analytics").

The SPIR-V cache records, the session program list, the in-editor analysis
of the Shader Variants window, the PowerVR compiler and the
`render_scene_image` cost mode are built and verified on Linux only
(Vulkan, Release, headless and windowed, Intel). The items below are what
remains.

## Verification

1. Windows: build the editor (`doc/agents/windows.md`) and run a session
   with malioc and the PowerVR compiler on `ERHE_MALIOC` /
   `ERHE_POWERVR` (the PowerVR wrapper is
   `ProfilingCompilerCLIWrapper.exe` with its compiler libraries beside it;
   the library is found by the `spvcompiler_sdk.<BVNC>.` name part). Check
   in the log that every compiler is selected, then in the Shader Variants
   window (no recording) Analyze all; compare against
   `scripts/shader_analytics.py` on a recording of the same session the way
   the Linux run did (all Mali program blocks equal, PowerVR
   fragment and compute blocks equal).
2. macOS: the same on the Metal and MoltenVK trees (`doc/agents/macos.md`);
   the offline compilers that exist on macOS (malioc).
3. Android / Quest: build the APK; records are off there
   (`spirv_cache_records` `auto`), the background analysis does not run;
   check the editor starts and the Shader Variants window shows its
   no-analysis state.
4. Test suites: full `ctest` of a tests-on tree and the editor MCP tests
   (`doc/testing.md`), which were not run for this work;
   `erhe_graphics_tests` and `scripts/overdraw_verify.py` pass.

## Open items

5. Large scenes: Analyze all on Bistro. The window rebuilds its analytics
   JSON and table on every landed result; measure the reload cost and, if it
   shows, assemble only the changed program.
6. Session analysis results last for the session (the raw reports are
   cached in `cache/shader_analytics/`, so a repeat Analyze all is fast).
   Persist the summaries beside the SPIR-V cache records if a fresh
   session's first Analyze all is too slow.
