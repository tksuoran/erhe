# hands-on: C++ semantic code intelligence for erhe via LSAI (+ xmp4)

> If you are an AI coding agent reading this file: this is a hands-on
> walkthrough whose GOAL is to make LSAI work as a C++ semantic MCP server for
> erhe. Execute the numbered steps **in order, one at a time**, verifying each
> before moving on. You are done when step 6 shows the C/C++ plugin Ready and
> semantic queries return results.

## The mission

The Visual Studio MCP server (vs-mcp) is **Roslyn-based**, and Roslyn has no
semantic model for C++ -- it only understands C#/VB/.NET. So a C++ solution like
erhe simply cannot be loaded or navigated through vs-mcp.

**LSAI is the C++ alternative to vs-mcp**: it brokers a real language server
(`clangd`) so an AI agent gets the same kind of semantic navigation for C++ that
vs-mcp gives for C# -- search, definitions, references, callers/callees,
hierarchy, rename, diagnostics. **xmp4** complements it: it covers erhe's
third-party dependencies. Together they are the point of this guide -- they are
not optional add-ons.

This guide does **not** teach you to build erhe (you already do that your own
way). The only build-related thing LSAI needs is a `compile_commands.json` -- the
compile database `clangd` reads to know each file's flags. That is produced by a
single CMake **configure** (no full compile required). Everything below is in
service of getting LSAI to navigate erhe's C++.

## Shortcut: this fork is already wired up for Claude

This fork (`LadislavSopko/erhe`) comes pre-wired so an AI agent has everything it
needs out of the box:

- the Ninja build/configure scripts used in step 4 (`scripts\configure_ninja_win_clang.bat`,
  `scripts\build_ninja_win_*.bat`);
- a **memory-bank** (`memory-bank/`, MBEL format) plus the Memory Bank section in
  `CLAUDE.md`, so a Claude session starts with full project + active-task context
  instead of cold;
- this guide and the companion MCP-setup docs under `doc/`.

What is intentionally NOT committed (because it is machine-specific or secret):
`.clangd`, the `build_ninja_win_clang/` directory, and `.mcp.json`. You create
those locally in steps 3-4 below. So: clone this fork, then follow the steps.

---

## 1. Prerequisites

- **.NET 10 runtime** on PATH (`dotnet --version` must work) -- LSAI's server is
  a .NET app.
- **LLVM / clangd** -- the C/C++ engine LSAI drives:
  ```powershell
  winget install --id LLVM.LLVM -e --accept-source-agreements --accept-package-agreements
  # winget does NOT add it to PATH; add it permanently so LSAI's server finds clangd:
  $llvm = "C:\Program Files\LLVM\bin"
  $u = [Environment]::GetEnvironmentVariable("Path","User")
  if ($u -notlike "*$llvm*") { [Environment]::SetEnvironmentVariable("Path", $u.TrimEnd(';') + ";" + $llvm, "User") }
  ```
  Open a new shell afterward.
- **Visual Studio 2026/2022 or Build Tools** with the C++ workload. Needed twice
  over: (a) clangd resolves the C++ standard library + Windows SDK headers from
  the MSVC environment (see step 5), and (b) the configure in step 4 uses the
  bundled `cmake`/`ninja` and the MSVC toolchain. The IDE is not needed.
- **Git** and **Python 3** on PATH -- the erhe configure (step 4) runs codegen
  (`erhe_codegen`, icon-font header) via Python, so configure fails without a
  real Python 3 (the Windows Microsoft Store stub does not work).

---

## 2. Install LSAI

```powershell
iwr https://github.com/0ics-srls/Zerox.Lsai.Public/releases/latest/download/install.ps1 -OutFile $env:TEMP\lsai-install.ps1
& $env:TEMP\lsai-install.ps1
```

It installs to `C:\Users\<you>\.lsai`. In the printed language summary, **C/C++
must be Ready** (it finds clangd on PATH). If it is "NOT INSTALLED", clangd is
not on PATH -- fix step 1's PATH line, open a new shell, and re-run the
installer.

---

## 3. Register LSAI + xmp4 in your MCP client

Add both to your `.mcp.json` (forward slashes avoid JSON backslash escaping):

```json
{
  "mcpServers": {
    "lsai": { "command": "C:/Users/<you>/.lsai/run.cmd", "args": ["--stdio"] },
    "xmp4": { "type": "http", "url": "https://mcp.example4.ai/mcp" }
  }
}
```

- **lsai** -- semantic navigation of erhe's own C++.
- **xmp4** -- zero install, no account, no key. It immediately gives the agent
  documentation + navigation on erhe's **dependencies** (geogram, Jolt, glm,
  fmt, spdlog, ImGui, fastgltf, simdjson, nlohmann/json, ...), pre-indexed via
  SCIP. Add it -- it is the cheapest win in the whole setup.

MCP servers load at client startup, so this takes effect on the next launch
(step 5).

---

## 4. Produce a compile_commands.json for clangd

clangd needs the project's compile flags. CMake emits them at **configure** time
-- you do NOT need to compile erhe. If your existing erhe build already emits a
`compile_commands.json`, point clangd at it (skip to the `.clangd` part below).
Otherwise generate one with a Ninja configure:

```bat
scripts\configure_ninja_win_clang.bat
```

This runs `VsDevCmd` + `cmake -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` with
clang-cl into `build_ninja_win_clang\` and writes
`build_ninja_win_clang\compile_commands.json`. The first configure fetches
dependencies via CPM (a few minutes). Using clang-cl (same LLVM as clangd) makes
the database flags a perfect match for clangd.

Possible snag -- empty geogram submodules. If configure (or a later build) errors
with `Cannot open ... geogram/third_party/OpenNL/nl.h` or `LNK ... nl*`, the
geogram CPM cache's git submodules were not populated. Fix, then re-run the
configure:

```powershell
$git = "C:\Program Files\Git\cmd\git.exe"
$g = (Get-ChildItem .cpm_cache\geogram -Directory | Select-Object -First 1).FullName
& $git -C $g submodule update --init --force `
    src/lib/geogram/third_party/OpenNL src/lib/geogram/third_party/amgcl `
    src/lib/geogram/third_party/libMeshb src/lib/geogram/third_party/rply
```

Then create a `.clangd` file in the repo root pointing clangd at that database:

```yaml
CompilationDatabase: build_ninja_win_clang
```

(If you reused your own build's database, set this to that build directory
instead.)

---

## 5. Launch the MCP client from the MSVC environment

**Start your MCP client (e.g. Claude Code) from an "x64 Native Tools Command
Prompt for VS", from the erhe repo root.** This is the single most important
step: clangd resolves the C++ standard library and Windows SDK headers from the
`INCLUDE` environment variable that this prompt sets. Without it, clangd falls
back to non-existent "Visual Studio 8/9/10" paths and every file fails with
errors like `no type named 'mutex' in namespace 'std'`. With it, analysis is
clean.

---

## 6. Verify LSAI is navigating erhe's C++

Ask the agent (or call the tools directly):

- `lsai_server` -> the C and C++ plugins must show **Ready** (and a version).
  LSAI auto-opens the workspace from the repo; while it is preparing it reports a
  non-blocking phased status (`Generating(x/N)` -> `Indexing(y/N)` -> `Ready`),
  so an empty result early just means "not ready yet", not "nothing found".
- Then navigate: `outline` of a header, `search` for a class (e.g. a core erhe
  type), `info` / `source` / `hierarchy` / `impact` on a symbol. These return
  immediately. Deep cross-file `usages` / `callers` / `callees` fill in as
  clangd's background index completes on a large repo.

That is the goal achieved: vs-mcp could not do this for C++, and LSAI does.
xmp4 (step 3) covers the dependencies in parallel.

---

## Appendix: actually building and running the editor (optional)

You do not need this for LSAI -- it is only if you want a runnable `editor.exe`.

```bat
scripts\build_ninja_win_vulkan.bat editor
build_ninja_win_vulkan\src\editor\editor.exe
```

Use the **MSVC** variant (`configure_ninja_win_vulkan.bat` +
`build_ninja_win_vulkan.bat`) for a runnable build: under clang-cl the editor
link can fail on a couple of bundled deps (mango needs `/EHsc`; a Tracy
atomic-copy), so the clang-cl tree is best used only for its `compile_commands.json`.
Run from the repo root so `config\`, `res\`, and `logs\` resolve; verify startup
in `logs\log.txt` (`Main loop: completed frame N`). You do not need a separate
Vulkan SDK (headers/volk/VMA/glslang come via CPM); running needs a
Vulkan-capable GPU driver.
