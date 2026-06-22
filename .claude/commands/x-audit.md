---
description: Conduct a Software Architecture, Foundations & Security Audit of a C++ project (KISS, DRY, YAGNI, Overengineering, Foundations, Security).
---
<BASE_RULE>
STEP 0 -- DO THIS BEFORE ANY OTHER TOOL CALL (including Read / Glob / Grep):
	1. Call `mcp__lsai__lsai_server`. Read the C++ workspace state and its ID.
	2. If the workspace is "Indexing" / "BootstrapInProgress" / "Loading", it is
	   warming up: wait a few seconds, call `lsai_server` again, repeat until it
	   is "Ready". (Empty results while it warms mean "not yet", not "absent".)
	3. STOP and tell the user ONLY if there is no C++ workspace at all, or it is
	   dead / Error after a re-check -- and even then do not silently grep.
	4. Take the `workspaceId` from the `lsai_server` output and pass it to EVERY
	   `mcp__lsai__*` call -- without it the calls fail with WorkspaceNotReady.

HARD BANS (violating any one INVALIDATES the entire audit result):
	- NEVER use Grep / Glob / Read to find, list, or read C++ symbols, types,
	  usages, definitions, call graphs, or `.cpp` / `.hpp` file contents.
	  For ALL C++ code introspection use `mcp__lsai__*` ONLY.
	- For external / third-party libraries indexed online, use `xmp4`
	  (`mcp__*xmp4*`) ONLY; fall back to internet search only if not indexed.
	- Grep / Read are permitted ONLY for: macro definitions, `CMakeLists.txt`
	  and other CMake files, `*.json`, `*.md`. Nothing else.
	- "fallback to grep / search" applies ONLY after a specific `mcp__lsai__*`
	  call has actually returned empty -- never as a first choice.

SELF-CHECK: if you have called Grep / Glob / Read on a C++ file (or to locate a
C++ symbol) before any `mcp__lsai__*` call, you have ALREADY failed this rule --
say so explicitly and restart from STEP 0.
</BASE_RULE>

- Skip the BASE_RULE and I will kick your honest ass straight back to STEP 0. Stop, reset your head, and do it right -- anything you produce otherwise is invalid.

## Your task

- Read `.claude/commands/mind-sets/audit.md` (the audit methodology)
- Read `.claude/commands/mind-sets/project-foundations-cpp.md` (the 16 C++ foundations the audit checks against -- it `$include`s `project-foundations.md`, read that too if referenced)
- If a Memory Bank exists in this repo, read it first for project context; if not, skip -- this command does not require one
- **Optional project settings:** if `.claude/commands/cpp-project.md` exists, read it and use its `@build` / `@test` / `@lint` / `@code-nav` values instead of the generic CMake/CTest commands
- **Evidence Discipline (no false negatives):** follow the "Evidence Discipline" section of audit.md -- use the language server / code intelligence (LSAI, clangd) not a single grep; scope to OWN code (exclude vendored / `_deps` / `.cpm_cache` / generated trees -- they inflate counts); NEVER emit a `Missing`/`Absent` verdict without running and quoting the targeted negative check (tests: `ctest -N`, `gtest_discover_tests` / `catch_discover_tests`, `enable_testing` + `*/test*/` targets); report a have/total ratio (e.g. "8/36 libs have tests") as `Partial`, never `Missing`
- **Code-level diagnostics (use LSAI to find problems IN the code, not just the build):** run `mcp__lsai__lsai_diagnostics` (minSeverity warning) and scope the result to OWN code (exclude vendored / SDK / generated / known-rotten trees). Discard the `-Wc++98-compat` / `-Wpre-c++NN-compat` pedantry the analyzer emits under its near-`-Weverything` profile, then report the SUBSTANTIVE warnings -- sign/signedness conversion, field/local shadowing, float `==`/`!=`, unsafe-buffer-usage / pointer arithmetic, unhandled `switch` enums, `const`-dropping casts, unused includes -- as a "Code-Level Diagnostics" finding with `file:line`. Tie any defect the real build hides (warnings-as-errors not on that compiler) back to Foundation #1.
- Use AskUserQuestion to gather project context if not provided (what the project is, primary build system, target platforms, what worries the user)
- Confirm activation with "# Architecture Audit active (C++)"

## Scope reminder (C++ specifics)

When auditing, weigh the C++ foundations concretely:
- Warnings-as-errors on every compiler (`-Werror` / `/WX`), `clang-tidy` with `WarningsAsErrors`
- Central dependency pinning (CPM / vcpkg versions / Conan lockfile) -- no moving branches
- RAII / no raw `new`-`delete`, no owning raw pointers in interfaces. Do NOT judge this
  from a raw `new`-vs-`delete` text count (unreliable, and forbidden by the BASE_RULE on
  C++ files): instead use LSAI to check that interface return/param types are owning smart
  pointers (`unique_ptr`/`shared_ptr`), and let LSAI `diagnostics` flag the actual misuse.
- **Network/attack surface**: do not assume "desktop/local app => no network surface". Use
  LSAI to search `listen` / `bind` / `Server` / `httplib`; if a server exists, read its
  bind address (loopback vs `0.0.0.0`), auth model, and payload caps before scoring Security.
- `std::expected`/typed errors over exceptions-for-control-flow
- File logging (spdlog), never `std::cout`/`printf`
- Zero-dependency core module, out-of-source deterministic builds
- Sanitizer coverage (ASan/UBSan, TSan in a separate build)
- No per-frame heap allocation in hot paths
