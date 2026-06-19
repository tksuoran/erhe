# Building erhe without the Visual Studio solution + installing the LSAI MCP server (Windows)

This document describes two related Windows workflows:

1. Building the `editor` target with the **Ninja** generator instead of the
   Visual Studio solution generator (no `.slnx`, no IDE).
2. Installing the **LSAI** MCP server so an AI coding agent (Claude Code,
   Cursor, ...) gets language-server-backed code intelligence for erhe's C++
   (via `clangd`).

> Important scope note: "without Visual Studio" here means **without the Visual
> Studio IDE / solution generator**. On Windows the compiler is still MSVC
> (`cl.exe`), which ships with Visual Studio or the standalone "Build Tools for
> Visual Studio". erhe has no GCC/Clang toolchain configured on Windows, so a
> genuinely VS-free toolchain is out of scope. What we avoid is generating and
> opening a `.slnx` solution: we drive the bundled `cmake` + `ninja` directly,
> inside the MSVC command-line environment.

---

## Part A -- Ninja build (no Visual Studio solution)

### A.1 Prerequisites

- Visual Studio 2026 (or 2022), or the standalone "Build Tools for Visual
  Studio", with the C++ workload. This provides:
  - `cl.exe` (the MSVC compiler) and the Windows SDK,
  - `cmake.exe`, `ninja.exe`, and `VsDevCmd.bat`, all bundled with VS.
- Git (for CPM dependency fetching, including submodules).
- The Vulkan SDK is fetched/handled by the build; no manual install needed for
  a default Vulkan configure.

On a default VS 2026 Community install the relevant tools live under:

```
C:\Program Files\Microsoft Visual Studio\18\Community\
    Common7\Tools\VsDevCmd.bat
    Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
    Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
```

Note: the VS **2026** install directory is named `18` (the internal major
version), not `2026`. Adjust the path for VS 2022 (`...\2022\Community\...`) or
for a non-Community edition.

### A.2 Wrapper scripts

Two helper scripts under `scripts\` encapsulate the flow. They first initialize
the MSVC command-line environment via `VsDevCmd.bat -arch=amd64`, then invoke
`cmake` / `cmake --build`. (The repo otherwise only ships Linux Ninja scripts;
these are the Windows equivalents.)

- `scripts\configure_ninja_win_vulkan.bat`
  - Generator: `-G Ninja`, single-config `-DCMAKE_BUILD_TYPE=Debug`.
  - Build directory: `build_ninja_win_vulkan\`.
  - Emits `compile_commands.json` (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) for
    clangd / LSAI.
  - Uses the same erhe options as the Vulkan VS preset (vulkan backend, jolt,
    bvh, tracy, sdl, openxr, freetype, fastgltf, imgui, plutosvg, harfbuzz,
    PCH on, SPIR-V on).
- `scripts\build_ninja_win_vulkan.bat <target>`
  - Re-initializes the MSVC env, then runs `cmake --build build_ninja_win_vulkan --target <target>`.

If the bundled `cmake`/`ninja` are not on PATH for the shell you run these from,
`VsDevCmd.bat` (called inside the scripts) puts them there for the duration of
the script.

### A.3 Configure and build

From any shell (the scripts set up MSVC themselves):

```bat
scripts\configure_ninja_win_vulkan.bat
scripts\build_ninja_win_vulkan.bat editor
```

The first configure fetches all CPM dependencies (several minutes). The result
is `build_ninja_win_vulkan\src\editor\editor.exe` (Debug).

### A.4 Known gotcha: empty geogram submodules in the CPM cache

geogram is fetched via CPM into `.cpm_cache\geogram\<hash>\`. geogram pulls some
third-party code (notably **OpenNL**) as git submodules. If a previously cached
geogram clone has its submodule working trees emptied (only the `.git` gitlink
left), CPM reuses that "dirty" cache and the build fails:

- Configure/compile error:
  `fatal error C1083: Cannot open include file: 'geogram/third_party/OpenNL/nl.h'`
- Or, if the headers are present but the `.c` sources were missing at configure
  time, a link error:
  `LNK2019: unresolved external symbol nlNewContext` (and many other `nl*`).

Fix -- restore the deleted submodule working trees in the cache, then
**re-configure** (so geogram's build picks up the OpenNL sources), then rebuild:

```powershell
$git = "C:\Program Files\Git\cmd\git.exe"
$g   = "<repo>\.cpm_cache\geogram\<hash>"   # the actual cached path from the configure log
& $git -C $g submodule update --init --force `
    src/lib/geogram/third_party/OpenNL `
    src/lib/geogram/third_party/amgcl `
    src/lib/geogram/third_party/libMeshb `
    src/lib/geogram/third_party/rply
```

```bat
scripts\configure_ninja_win_vulkan.bat
scripts\build_ninja_win_vulkan.bat editor
```

The re-configure is required: CMake records geogram's source list at configure
time, so OpenNL's `.c` files must exist *before* configure for them to be
compiled and linked.

### A.5 Run the editor

Run from the repo root (so `config\` and `res\` resolve, and logs land in
`logs\`):

```powershell
Start-Process -FilePath "build_ninja_win_vulkan\src\editor\editor.exe" -WorkingDirectory "<repo>"
```

Verify startup via the log (stdout redirection alone is empty -- spdlog writes
to the file sink):

```powershell
Get-Content logs\log.txt -Tail 20
```

A healthy run shows Vulkan init, shader/SPIR-V cache lines, and
`[editor.startup] Main loop: completed frame N` increasing.

---

## Part B -- LSAI MCP server

LSAI (https://github.com/0ics-srls/Zerox.Lsai.Public) is an MCP server that
brokers language-server (LSP) intelligence to an MCP client. For erhe's C++ it
drives `clangd`, which needs a `compile_commands.json` (see Part A.2).

### B.1 Prerequisites

- **.NET 10 runtime** on PATH (`dotnet --version` must work).
- Per-language toolchains for the languages you want analyzed. For erhe only
  C/C++ matters, which needs `clangd` (see B.3). The installer also picks up
  Python (ty / pyright), Java (jdtls), and TS/JS if their toolchains are
  present.

### B.2 Install (Windows, release procedure)

```powershell
iwr https://github.com/0ics-srls/Zerox.Lsai.Public/releases/latest/download/install.ps1 -OutFile $env:TEMP\lsai-install.ps1
& $env:TEMP\lsai-install.ps1
```

This installs into `C:\Users\<you>\.lsai\` (launcher `run.cmd`, the server,
language servers, and `config.json`). The installer prints a per-language
"Ready / Missing" summary at the end.

### B.3 Install clangd (required for C/C++)

If the summary lists C/C++ as missing, install LLVM (which provides `clangd`),
then re-run the installer so it is detected:

```powershell
winget install --id LLVM.LLVM -e --accept-source-agreements --accept-package-agreements
# clangd lands in C:\Program Files\LLVM\bin\clangd.exe
& $env:TEMP\lsai-install.ps1
```

After re-running, the summary should show `C/C++  clangd  (system)`.

Note: `winget` updates the machine PATH, but the *current* shell will not see
`clangd` until a new process is started. Start a fresh shell (or a fresh MCP
client session) so the LSAI launcher inherits `clangd` on PATH.

### B.4 Register the MCP server with the client

LSAI runs over stdio. For Claude Code, add it to the MCP config alongside any
existing servers (forward slashes avoid JSON backslash escaping):

```json
{
  "mcpServers": {
    "lsai": {
      "command": "C:/Users/<you>/.lsai/run.cmd",
      "args": ["--stdio"]
    }
  }
}
```

### B.5 Make clangd find the compile database

clangd searches the project root (and a few standard `build/` names) for
`compile_commands.json`. Since the Ninja build dir is non-standard
(`build_ninja_win_vulkan`), add a `.clangd` file in the repo root pointing at
it (this stays in sync with rebuilds, unlike a copy):

```yaml
CompilationDatabase: build_ninja_win_vulkan
```

Both `.clangd` and `build_ninja_win_vulkan/` are listed in `.gitignore`.

### B.6 Activate

LSAI analyzes whatever directory the MCP client is launched from, and MCP
servers are loaded at client startup. So:

```powershell
cd <repo>
claude
```

A fresh session loads the `lsai` server, inherits `clangd` on PATH, and uses the
repo's `.clangd` -> `compile_commands.json` for accurate C++ analysis.

---

## Quick reference

```bat
:: build (Ninja, Vulkan, no VS solution)
scripts\configure_ninja_win_vulkan.bat
scripts\build_ninja_win_vulkan.bat editor

:: run
build_ninja_win_vulkan\src\editor\editor.exe
```

```powershell
# LSAI install / refresh
& $env:TEMP\lsai-install.ps1     # after downloading install.ps1 from the latest release
```
