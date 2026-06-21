---
description: Debug any C++ / native problem with Protocol D (RIDHV). Systematic, evidence-first debugging for build errors, crashes, sanitizer reports, GPU/Vulkan validation failures, and logic bugs. No guessing -- quote exact errors, isolate the failing layer, one change at a time.
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
- Read `.claude/commands/mind-sets/debug-protocol.md` (Protocol D -- the methodology, single source of truth for the rules)
- If a Memory Bank exists, read it for context; otherwise skip (not required)
- **Optional project settings:** if `.claude/commands/cpp-project.md` exists, read it and use its `@build` / `@test` / `@asan` / `@code-nav` / `@logs` / `@platform` values instead of the generic commands below

## Purpose

Universal C++ debugging. No branches, no workflow overhead. Just Protocol D
(RIDHV: Read -> Isolate -> Docs -> Hypothesize -> Verify) applied to find and
understand the problem before changing anything.

Use this when:
- A build/link fails and the cause is not obvious
- The program crashes, hangs, or corrupts memory
- A sanitizer or validation layer fires
- Output is wrong and you don't know which component owns the bug

## Steps

### 1. Identify Problem Type

Ask the user:
```
What's broken?
1. Build / compile error (won't compile)
2. Link error (compiles, won't link)
3. Runtime crash / hang / wrong result
4. Sanitizer or validation report (ASan/UBSan/TSan, Vulkan/GL validation)
5. Don't know (let's find out)

Which one?
```

### 2. Gather Initial Info

**Build / compile:** the FIRST compiler error (not the cascade after it), the
exact file:line, the failing translation unit, the compiler + flags.
**Link:** the exact unresolved/duplicate symbol, which target, static vs shared.
**Runtime:** how to reproduce, the stack trace (gdb/lldb), the last log line
before the failure, the build config (Debug/Release, sanitizers on?).
**Sanitizer/validation:** the exact report header (ASan error type, the VUID for
Vulkan, the UBSan kind) and the first frame in *your* code.

### 3. Pre-Debug Checklist

```
Build metadata:
- [ ] Reproducible build? (out-of-source dir, known config)
- [ ] compile_commands.json present? (clangd / tooling is blind without it:
      cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
- [ ] On Windows: running inside the MSVC env (x64 Native Tools)? Outside it,
      the toolchain/clangd can't resolve the STL + Windows SDK and EVERYTHING
      looks broken -- rule this out FIRST.

Tools ready:
- [ ] Debug build with symbols (-g / /Zi), or a sanitizer build for memory bugs
- [ ] gdb / lldb ready; know where the logs are (spdlog file, not stdout)
- [ ] Can reproduce deterministically
```

### 4. Protocol D - READ (find ALL error sources)

| Layer | Where to look | Tool / command |
|-------|---------------|----------------|
| Compile | First compiler diagnostic | build output -- the FIRST error, ignore the cascade |
| Link | Unresolved/duplicate symbol | linker output; `nm` / `dumpbin /symbols` |
| Runtime crash | Stack trace + faulting address | `gdb --args <bin>` / `lldb`, `bt`, `frame`, `p` |
| Memory / UB | Sanitizer report | ASan/UBSan build, read the first own-code frame |
| Data race | TSan report | separate `-fsanitize=thread` build |
| GPU | Validation layer (VUID) | enable the validation layer; the last "creating X" log before the error names the culprit |
| Logic | App log file | spdlog file output, not `std::cout` |

**Output required:**
```
**COMPILE ERROR**: [first exact diagnostic + file:line, or "none"]
**LINK ERROR**: [exact symbol, or "none"]
**RUNTIME ERROR**: [signal/exception + top own-code frame, or "none"]
**SANITIZER/VALIDATION**: [exact report header, or "none"]
**LOG TAIL**: [last meaningful log line before failure]
```

### 5. Protocol D - ISOLATE (which layer/component OWNS it)

Trace from input to failure and find the boundary:
```
inputs valid? -> construction OK? -> call reaches the function? -> invariants hold?
  -> resource/handle valid? -> output produced? -> consumed correctly?
```
- Last step that worked = OK
- First step that failed = FAIL  <- target component

Narrow with a minimal repro: shrink to the smallest input/codepath that still
fails. For memory bugs, trust the sanitizer's allocation/free stacks over your
intuition.

**Output required:**
```
**FAILING COMPONENT**: [target/file/function]
**LAST SUCCESS**: [what worked]
**FIRST FAILURE**: [what failed]
**LOCATION**: [file:line]
```

### 6. Protocol D - DOCS

If a third-party library / compiler / API is involved: check the official docs,
verify the version actually linked (a pinned tag in your dependency manifest),
and look for known behavior. Skip if it is clearly your own logic.

### 7. Protocol D - HYPOTHESIZE (before touching any code)

```
**HYPOTHESIS**: [the single cause you believe explains it]
**REASONING**: [cause -> effect chain that explains ALL symptoms]
**PREDICTION**: [what will change after the fix -- must be testable]
```
Rules: falsifiable, explains every symptom, no "let's try" -- only "I believe X
because Y". Confirm with the user before changing code.

### 8. Protocol D - VERIFY (ONE change at a time)

```
**CHANGE**: [single change]
**RESULT**: [actual outcome -- re-run the repro / sanitizer]
**PREDICTION MATCH**: [yes/no]
**CONCLUSION**: [what we learned]
```
Prediction wrong -> back to READ with the new evidence. Never stack two changes
before verifying the first.

### 9. Decision Point

```
Problem found: [summary]   Location: [file:line]   Cause: [explanation]

Options:
1. Fix now (small, contained)
2. Open a ticket for a larger fix
3. It's actually a missing feature -> plan it (x-tddab)
4. Need more investigation
```

## Rules
- **Protocol D always** -- READ, ISOLATE, DOCS, HYPOTHESIZE, VERIFY
- **No guessing** -- evidence from compiler/sanitizer/debugger/logs required
- **Quote exact errors** -- never paraphrase a diagnostic or a VUID
- **The FIRST compiler error**, not the cascade it triggers
- **One change at a time** -- verify before the next
- **Memory bugs: use a sanitizer build**, don't eyeball pointer arithmetic
- **Windows: confirm the MSVC environment before blaming the code**

## Quick Reference
```
RIDHV = Read -> Isolate -> Docs -> Hypothesize -> Verify
         ^                                          |
         +---------------- if wrong ----------------+

Compile -> first diagnostic   Link -> nm/dumpbin
Runtime -> gdb/lldb bt        Memory/UB -> ASan/UBSan
Races  -> TSan (separate)     GPU -> validation VUID + last log
Always ask: "Which component OWNS this problem?"
```
