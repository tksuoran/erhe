---
description: Review a C++ TDDAB plan for methodology compliance, dependency ordering, self-sufficiency, and conformity to the C++ foundations -- and cross-check it against the real codebase before any code is written. Use after x-tddab and before x-develop.
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

- if you dont apply BASE_RULE: stop here you are fucking idiot!

## Prerequisites
- Read `.claude/commands/mind-sets/tddab-planner.md` + `.claude/commands/mind-sets/cpp-tddab-overlay.md` (the rules the plan must satisfy)
- Read `.claude/commands/mind-sets/project-foundations-cpp.md` (project conformity)
- If a Memory Bank exists, read it for context; otherwise skip
- **Optional project settings:** if `.claude/commands/cpp-project.md` exists, read it and use its `@code-nav` / `@build` / `@test` values where relevant

## Steps

### 1. Read the methodology
The rules in `tddab-planner.md` + `cpp-tddab-overlay.md` are the ONLY source of
truth for this review. Do not apply rules from memory or training.

### 2. Find the plan
Ask the user for the plan file (e.g. `tddab-plan.md`) if not given.

### 3. Review -- apply rules from the methodology (no hardcoded checklist)

#### A. Structural correctness
- All required tags present per the planner grammar: `mission` (once), one
  `block id` each, `intro`, `red` (with `- test:` lines), `success` (a `- [ ]`
  checklist)? (There is NO `verify` tag -- verifiable outcomes live in `success`.)
- Block IDs unique and well-formed (`NN-kebab-case`)?
- Is the `mission` comprehensive enough for clean-context execution, and each
  `intro` self-sufficient?

#### B. Dependency & ordering (CRITICAL)
- For each block: does it use a type/function/file defined in a LATER block?
  If yes -> **dependency error**; the block cannot execute in this order.
- Does the execution order match the real dependencies?

#### C. Self-sufficiency
- Can each block be understood with ZERO context beyond its mission?
- Are file paths complete? Any "as we discussed" / "previous block" leakage?

#### D. C++ completeness (per the overlay)
- Both the value AND the error/empty path tested for every `std::expected`/`optional`?
- Throwing paths asserted with `REQUIRE_THROWS_AS`?
- One test target per module? Does the `success` checklist include build-clean
  (0 warnings) + `clang-tidy` + ASan/UBSan outcomes?
- "..." that skips a DECISION (not boilerplate) -> flag it.

#### E. Foundations conformity
- RAII / no raw new-delete, no owning raw pointers in interfaces
- File logging not `std::cout`; typed errors not stringly-typed
- No `file(GLOB)`; sources listed explicitly; out-of-source build

### 4. Code cross-check -- plan vs real codebase (CRITICAL)
Steps 1-3 review the plan in isolation. Now verify it against the actual code:
- **Existence** -- for each symbol the plan says it will *use/call/extend/include*
  (not the ones it creates), look it up (clangd/LSP, else grep). Not found ->
  **dependency error** (typo/hallucination/renamed). Found -> confirm the
  signature/shape matches how the plan uses it.
- **File paths** -- each path the plan will *edit* must exist; missing -> report.
- **Reuse** -- for the main new types the plan *creates*, search for an existing
  equivalent; if one exists -> flag a reuse opportunity (extend, don't duplicate).
- **Third-party APIs** -- verify the signature against the actually-pinned
  library version.

### 5. Report

Issues found:
```
PLAN REVIEW -- ISSUES FOUND
[Dependency / Ordering]   - block IDs + explanation
[Code Cross-Check]        - block ID + symbol/path absent, or reuse opportunity
[Structural]              - ...
[C++ Completeness]        - ...
SUGGESTED FIXES: 1. ...  2. ...
Fix these before x-develop.
```
Plan OK:
```
PLAN REVIEW -- APPROVED
OK Structure  OK Dependencies  OK Self-sufficiency
OK C++ completeness (expected/throw paths, sanitizers in VERIFY)
OK Code cross-check (symbols/paths exist, no unintended duplication)
OK Foundations conformity
Ready for x-develop.
```

## Rules
- The methodology file is the ONLY source of truth -- no stricter/looser invented rules
- Be strict on dependency ordering (causes real compile failures) and self-sufficiency
- Cross-check against real code, not memory -- a symbol/path either exists or it doesn't
- Be lenient on style -- the methodology defines what "complete" means
- Explain WHY something is wrong; suggest specific fixes; if good, say so and move on
