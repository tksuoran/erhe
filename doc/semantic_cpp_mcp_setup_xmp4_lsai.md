# Semantic C++ code intelligence for Claude Code: xmp4 + LSAI

Purpose: give an AI coding agent (Claude Code, Cursor, ...) real *semantic*
navigation of a C++ project. The Visual Studio MCP server is Roslyn-based and
has no semantic model for C++ (it only understands C#/VB/.NET), so on a C++
solution it reports "no solution loaded". The two MCP servers below fill that
gap and complement each other:

- **LSAI** -- brokers a real language server (clangd) for *this* repo's C++.
  Local, project-specific: outline, info, source, usages, callers, callees,
  hierarchy, rename, diagnostics.
- **xmp4** -- SCIP-based code intelligence for *public* libraries (900+ OSS
  libs indexed). Use it to navigate the internals of third-party dependencies
  the project pulls in (e.g. simdjson, nlohmann/json, fmt, ...).

Rule of thumb for the operating agent: **LSAI for the project's own code, xmp4
for its dependencies.**

This guide was written from a field test on `erhe` (Windows, ~1700 translation
units, Ninja build). The non-obvious gotchas below are the ones that actually
blocked it.

---

## Part 1 -- xmp4 (trivial, do this first)

No install, no API key, no signup. It is a hosted HTTP MCP server.

Add to `.mcp.json` (project root) or `~/.claude/mcp.json`:

```json
{
  "mcpServers": {
    "xmp4": {
      "type": "http",
      "url": "https://mcp.example4.ai/mcp"
    }
  }
}
```

Optional Claude Code skill (improves how the agent uses xmp4):

```bash
mkdir -p ~/.claude/skills/xmp4 && curl -sfL \
  https://example4.ai/xmp4-skill.md \
  -o ~/.claude/skills/xmp4/SKILL.md
```

Usage pattern for the agent: `xmp4_projects(query=...)` to find a library's
project id, then `xmp4_search` / `xmp4_source` / `xmp4_usages` against that id.
If a dependency is not indexed, popular OSS libs are added within a day via a
GitHub issue on the xmp4 project.

---

## Part 2 -- LSAI (the real setup)

LSAI runs over stdio and drives clangd for C/C++. clangd needs a
`compile_commands.json` and -- critically on Windows -- the MSVC environment.

### 2.1 Install

Prereqs: .NET 10 runtime on PATH (`dotnet --version` works).

Windows:

```powershell
iwr https://github.com/0ics-srls/Zerox.Lsai.Public/releases/latest/download/install.ps1 -OutFile $env:TEMP\lsai-install.ps1
& $env:TEMP\lsai-install.ps1
```

Installs to `C:\Users\<you>\.lsai\` (launcher `run.cmd`, server, language
servers, `config.json`). The installer prints a per-language Ready/Missing
summary.

### 2.2 clangd is required for C++ -- and must be on PATH

If C/C++ shows "NOT INSTALLED", install LLVM (provides clangd), then re-run the
installer so it is detected:

```powershell
winget install --id LLVM.LLVM -e --accept-source-agreements --accept-package-agreements
& $env:TEMP\lsai-install.ps1
```

GOTCHA: `winget install LLVM.LLVM` does NOT add `C:\Program Files\LLVM\bin` to
PATH by default. Add it to the user PATH permanently, otherwise the LSAI server
(spawned by Claude Code) won't find clangd and C/C++ stays "NOT INSTALLED":

```powershell
$llvm = "C:\Program Files\LLVM\bin"
$u = [Environment]::GetEnvironmentVariable("Path","User")
if ($u -notlike "*$llvm*") { [Environment]::SetEnvironmentVariable("Path", $u.TrimEnd(';') + ";" + $llvm, "User") }
```

### 2.3 Register the MCP server

stdio. Add alongside xmp4 (forward slashes avoid JSON backslash escaping):

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

### 2.4 Provide a compile_commands.json + .clangd

clangd needs the build's compile flags. Generate them with a Ninja build that
exports the compile database (works with MSVC `cl` or, for the cleanest clangd
match, `clang-cl`):

```
cmake -G Ninja -B <build_dir> -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...
```

Then a `.clangd` in the repo root pointing at that build dir (so clangd finds
the DB even if the dir name is non-standard):

```yaml
CompilationDatabase: <build_dir>
```

### 2.5 THE critical Windows requirement: launch from the MSVC environment

clangd resolves the C++ standard library and Windows SDK headers from the
`INCLUDE` environment variable. If Claude Code (and therefore the LSAI server
and clangd) is launched from a plain shell, clangd cannot find them -- it falls
back to phantom "Visual Studio 8/9/10" paths and every TU fails with errors
like `no type named 'mutex' in namespace 'std'`.

Fix: **launch Claude Code from an "x64 Native Tools Command Prompt for VS"**
(which sets INCLUDE/LIB/PATH), from the repo root. Verified effect on erhe:
`clangd --check` on one file went from 26 errors (toolchain not found) to 0 real
errors once INCLUDE was set. This single step is the difference between LSAI C++
working or not.

---

## Part 3 -- Notes for the operating Claude (how to actually use it)

- **Confirm the plugin is up first:** `lsai_server` -> C/C++ must be "Ready"
  (not "NOT INSTALLED"). If not Ready, clangd isn't on the server's PATH (see
  2.2) -- a restart from the VS Native Tools prompt is usually the fix.
- **Use the real workspace id, not "".** `lsai_search(workspaceId="")` fails
  with WorkspaceNotReady. Call `lsai_workspace_list` and use the actual id
  (e.g. `erhe-c-1` for the C++ workspace at the right drive).
- **File-scoped queries work immediately; project-wide ones need the index.**
  `outline` / `info` / `source` (with a `filePath`) return instantly and
  accurately. `search` / `usages` / `callers` / `hierarchy`-by-name depend on
  clangd's `--background-index`, which takes time to populate on a large repo.
  While indexing, an empty result means "index not ready", NOT "doesn't exist"
  -- do not conclude absence. Give the index time, or fall back to file-scoped
  ops plus ripgrep.
- **LSAI vs xmp4:** symbol defined in this repo -> LSAI. Symbol from a
  dependency (simdjson, fmt, jolt, ...) -> find the project with
  `xmp4_projects` then `xmp4_search`/`xmp4_source`.

### Known issue and fix (LSAI MJSF staleness fixpoint)

On a project that contains any non-clangd sub-project whose tool is
unresolvable -- e.g. erhe has an `android-project` (Java), and `jdtls` is not a
real Eclipse jdtls install -- LSAI <= 1.0.178 marks the generated MJSF "stale"
on every `workspace_open` and re-runs the analyzer over all projects (~88 s on
erhe's 195 projects), every single time. Root cause: the staleness check keyed
on a tool whose availability depends on the environment, so the state written
by Generate() immediately re-satisfies the "stale" condition (a non-converging
fixpoint).

- Fixed in **v1.0.179** (staleness governs only the project graph; tool
  availability is refreshed separately and never triggers a full re-analyze).
- Workaround on <= 1.0.178: add a stub tool entry to
  `<repo>/.lsai/mjsf.json` so the check passes, e.g.
  `"jdtls": { "path": "STUB", "runtime": { "kind": "java" } }` under `tools`,
  or set `LSAI_JDTLS_HOME` to a real Eclipse jdtls directory. Effect on erhe:
  `workspace_open` dropped from 88832 ms to 172 ms.

---

## Minimal combined .mcp.json

```json
{
  "mcpServers": {
    "xmp4": { "type": "http", "url": "https://mcp.example4.ai/mcp" },
    "lsai": { "command": "C:/Users/<you>/.lsai/run.cmd", "args": ["--stdio"] }
  }
}
```

Then: launch Claude Code from the VS Native Tools prompt, in the repo root, with
a `.clangd` -> `compile_commands.json` in place. `lsai_server` to confirm C/C++
Ready, and you have semantic C++ for the repo (LSAI) plus its dependencies
(xmp4).
