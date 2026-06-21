# LSAI usage playbook (erhe, C++)

How to use the LSAI MCP server correctly for semantic C++ navigation on this
repo. Written from **measured behavior** on 2026-06-21 (LSAI v1.0.187, workspace
`erhe-c-1`), not from assumption. Companion to `doc/semantic_cpp_mcp_setup_xmp4_lsai.md`
(which covers install/setup). This file is about *how to query*.

## TL;DR rule

For any **C++ symbol / type / usage / "does X exist"** question on erhe's OWN
code: **use LSAI first**. grep is the fallback ONLY for things LSAI cannot see
(see the division-of-labor table). A negative verdict ("absent", "0 uses") is
valid ONLY from a precise LSAI query or a path-scoped search -- never from an
unscoped search or a macro lookup.

## How LSAI sees this repo (the key mental model)

- LSAI runs in **parasite mode**: it auto-opens projects from `.lsai/mjsf.json`
  at startup. **Do NOT call `lsai_workspace_open`** -- it is legacy and has
  blocked for ~88-164s historically. Confirm readiness with `lsai_server`
  (C++ plugin `Status: Ready`) instead.
- `mjsf.json` contains **287 projects** built from the clangd compile-commands
  database. That index covers **everything in compile_commands.json**:
  - erhe's own code -- projects with ids like `c++:src/erhe/log/erhe_log`,
    `c++:src/erhe/geometry/erhe_geometry`, and the `editor` tree, with
    `name` = the leaf dir (`erhe_log`, `erhe_geometry`, ...).
  - **All vendored deps** -- `.cpm_cache/{geogram,spdlog,fmt,glslang,sdl,...}`
    and `build_*/_deps/...`.
  - **System headers** -- `Program Files/.../MSVC/...`, Windows Kits.
- **Consequence (critical):** a bare `lsai_search` for a common term
  (`Result`, `expected`, `Mesh`) returns results **dominated by vendored / system
  symbols**. That is the index working as designed, NOT a bug. You must scope.

## Tool -> question map (measured)

| Question | Tool | Scoping needed? |
|---|---|---|
| "Does class/enum/function `X` exist? what's its signature/file?" | `lsai_info` symbolName=`X` | No -- precise, returns the def |
| "What are the members of type `T`?" | `lsai_outline` typeName=`T` | No -- precise |
| "Where is `X` used / who references it?" | `lsai_usages` symbolName=`X` | No for real symbols; **fails on macros** |
| "Who calls / is called by `X`?" | `lsai_callers` / `lsai_callees` | No |
| "Find symbols whose name matches `N`" (fuzzy discovery) | `lsai_search` query=`N` | **YES -- scope or the result is mostly vendored noise** |
| "What's in file `F` (members, refs, risk)?" | `lsai_context` filePath=`F` | No |
| "Count uses of a **macro** (`ERHE_VERIFY`, `ERHE_FATAL`)" | **grep** | n/a -- clangd does not index macros |
| "Compiler flags in CMake / keys in JSON / CI in YAML" | **grep** | n/a -- LSAI indexes only C++ |
| "Third-party library API (fastgltf, simdjson, Jolt...)" | **xmp4** | n/a -- LSAI is own-code; xmp4 is libraries |

### Proven examples

- `lsai_info ICollision_shape` -> `src/erhe/physics/.../icollision_shape.hpp:59 class ... erhe::physics`. Clean.
- `lsai_usages App_context` -> **582 usages in 211 files**. Works on a real class.
- `lsai_usages ERHE_VERIFY` -> `ERROR [SymbolNotFound]`. Macro -> use grep (`grep -rl ERHE_VERIFY src/erhe src/editor` = 207 files).
- `lsai_search "expected"` (unscoped) -> flood of `.cpm_cache/fastgltf`, Windows SDK, MSVC STL. Only own-code hit was a vendored imgui-node-editor enum. **The correct read is "filter to src/erhe + src/editor, then judge".**

## Scoping a `lsai_search` to own code -- two recipes

**Recipe A -- you know the library:** pass `projectName` = the leaf project name.
```
lsai_search(workspaceId="erhe-c-1", query="Mesh", projectName="erhe_scene")
```
Own-lib project names are `erhe_<lib>` (e.g. `erhe_geometry`, `erhe_item`,
`erhe_scene`, `erhe_renderer`). Note some names are not unique across the 287
projects (vendored subdirs reuse `src`, `windows`, `basic`...), so projectName is
reliable mainly for the distinctive `erhe_*` names.

**Recipe B -- cross-cutting search:** run unscoped, then **keep only own-code
paths** and discard vendored/system. Own code = `src/erhe/` and `src/editor/`.
Exclude these (they live under `src/` but are vendored/dead, NOT erhe code):
`src/imgui`, `src/imgui_gradient`, `src/mango`, `src/quickhull`,
`src/RectangleBinPack`, `src/tinyexpr`, `src/hidapi`, `src/spng`, `src/mINI`,
`src/rapidjson`, `src/rendering_test` (rotten), plus `.cpm_cache/`, `build_*/`,
`Program Files/`.

## Division of labor (do not cross the streams)

- **LSAI** = erhe's OWN C++ code (symbols, types, usages, callers, outline).
- **xmp4** = THIRD-PARTY library internals/APIs. Golden path:
  `xmp4_projects -> xmp4_search -> xmp4_info -> xmp4_tests_for -> xmp4_view`,
  **always pass `file_path`** (~80x faster resolver); prefer `xmp4_view` over
  `xmp4_grep`.
- **grep** = non-C++ files (CMake, JSON/TOML, YAML, `.gitignore`), and **C/C++
  macros** (LSAI/clangd is blind to macros). Declaring "grep, lower confidence"
  is only needed when LSAI is down -- for these categories grep is simply the
  right tool, not a fallback.

## Anti-patterns that produced wrong/empty results here

1. Treating the code-nav preflight as a one-time gate, then reverting to grep for
   C++ symbol verdicts. The preflight binds **every** navigation step.
2. `lsai_usages` on a **macro** -> SymbolNotFound, misread as "absent".
3. **Unscoped** `lsai_search` for a common term -> vendored/system flood,
   misread as signal or noise instead of being path-filtered.
4. Not reading `.lsai/mjsf.json` first -- it tells you up front that the index
   spans the whole dependency graph, so search MUST be scoped.
