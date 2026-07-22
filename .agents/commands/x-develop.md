---
description: Implement a C++ TDDAB plan, block by block, as a senior C++ developer -- RED (failing Catch2 test) -> GREEN (minimum code) -> VERIFY (build clean, ctest, clang-tidy, sanitizers). Loads the C++ senior mindset and foundations. Use once the plan is reviewed.
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

HARD BANS (violating any one INVALIDATES the result you produce):
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

## Prerequisites
- Read `.claude/commands/mind-sets/cpp-senior.md` (the C++ senior mindset -- the rules you code by)
- Read `.claude/commands/mind-sets/common-coding-style.md` (cross-language rules: immutability, KISS/DRY/YAGNI, file limits)
- Read `.claude/commands/mind-sets/project-foundations-cpp.md` (the foundations the code must satisfy)
- If the methodology is TDDAB: read `.claude/commands/mind-sets/cpp-tddab-overlay.md`
- If a Memory Bank / plan file exists, read it for context; otherwise work from the plan the user points you to
- **Optional project settings:** if `.claude/commands/cpp-project.md` exists, read it and use its `@build` / `@configure` / `@test` / `@lint` / `@asan` values in the per-block VERIFY loop instead of the generic CMake/CTest commands

## Purpose

Execute the plan and produce working, foundation-conformant C++. One block at a
time, never running ahead of a green build.

## Per-block loop (TDDAB)

For each block in the plan, in order:

1. **RED** -- write the failing test first. Catch2 `TEST_CASE` with `[tag][fast]`
   labels, `REQUIRE` / `REQUIRE_THROWS_AS`. Build it; confirm it FAILS for the
   right reason (the symbol/behavior doesn't exist yet), not a typo.
2. **GREEN** -- write the MINIMUM code to pass: the header contract + the source.
   Follow the senior rules exactly:
   - RAII, smart pointers, no raw `new`/`delete`; `class` over `struct`
   - `m_` members, `Snake_case` types, const-correct, explicit types over `auto`
   - `std::expected<T,E>` / typed errors; file logging (spdlog), never `std::cout`
   - No per-frame heap allocation in hot paths
   Build; confirm the test now PASSES.
3. **VERIFY** -- before moving to the next block:
   ```
   cmake --build build                          # 0 warnings / 0 errors
   ctest --test-dir build --output-on-failure   # all green
   clang-tidy -p build $(git ls-files '*.cpp')  # static analysis clean
   ctest --test-dir build-asan                  # ASan/UBSan build -- no leaks / UB
   ```
4. **COMMIT** (if the user works in git) -- one block = one focused commit.

Do NOT start the next block until the current one is fully green + clean.

## If there is no plan

If the user wants to develop without a TDDAB plan, still work test-first where
practical, follow the senior mindset and foundations, and keep changes small and
verifiable. Prefer writing a quick plan with `x-tddab` first for anything
non-trivial.

## Rules
- **Test-first** -- RED before GREEN, always; the test must fail before it passes
- **Minimum code to green** -- no speculative generality (YAGNI)
- **0 warnings is a gate**, not a goal -- `-Werror`/`/WX`, clang-tidy clean
- **Sanitizers each VERIFY** -- a leak/UB is a failing block
- **Follow existing patterns and naming** in the target codebase
- **Fix the cause, not the symptom** -- a defensive check that hides the root is rejected
- **One block at a time** -- never batch blocks past a red build
- **Ask when unsure** -- use AskUserQuestion rather than guessing the intent
