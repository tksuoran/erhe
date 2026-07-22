---
description: Plan a C++ feature with the TDDAB methodology (Test-Driven Development As Blocks) -- produce a block-by-block plan where each block is a self-sufficient RED -> GREEN -> VERIFY unit. Use before x-develop when you want a reviewable plan instead of coding ad hoc.
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
- Read `.agents/commands/mind-sets/tddab-planner.md` (the base TDDAB methodology -- single source of truth for plan structure)
- Read `.agents/commands/mind-sets/cpp-tddab-overlay.md` (C++ specifics: Catch2/CTest, expected/optional, sanitizers in VERIFY)
- Read `.agents/commands/mind-sets/project-foundations-cpp.md` (the foundations the plan must respect)
- If a Memory Bank exists, read it for context; otherwise skip
- **Optional project settings:** if `.agents/commands/cpp-project.md` exists, read it and use its `@build` / `@test` / `@asan` values in the VERIFY steps of the plan

## Purpose

Turn a feature request into a TDDAB plan: an ordered set of blocks, each one a
small contract proven by a failing test (RED), made to pass with the minimum
code (GREEN), then hardened (VERIFY). Every block must execute on a clean
context -- no dependency on a later block.

## Steps

1. **Clarify the feature** -- ask the user about anything ambiguous (scope,
   target API, where it plugs into the existing code).
2. **Decompose bottom-up** -- list the smallest provable units. For C++ that is
   usually: value types/DTOs -> domain functions -> the service/composition.
   A block builds only on earlier blocks.
3. **Write the plan** following the exact `tddab-planner.md` grammar -- the tags
   are `mission`, `block`, `intro`, `red`, `success` (there is NO `verify` tag):
   - `mission` -- ONCE at the top: full project context, enough to execute any
     block on a clean context
   - one `block id="NN-kebab-name"` per unit, each containing:
     - `intro` -- what the block does, deps on earlier blocks, files it touches
     - `red` -- `- test:` lines, the contract as Catch2 `TEST_CASE`s
     - an `### Implementation` markdown section -- reference C++ (types,
       signatures, key logic); detailed enough to produce compilable code, no
       need for perfect imports/boilerplate
     - `success` -- a `- [ ]` checklist of verifiable outcomes (tests pass,
       0 warnings, clang-tidy clean, ASan/UBSan clean)
4. **Respect the C++ rules** from the overlay: test both the value and the
   error/empty path of every `std::expected`/`optional`; one test target per
   module (`add_executable(<lib>_tests)`); RAII in tests (stack SUT, no manual
   `delete`); cover the cleared-scratch reuse path for hot-path code.
5. **Self-check** before handing off: unique block IDs, no forward dependencies,
   each block self-sufficient, no unresolved "Option A or B" decisions.
6. **Write the plan** to a file the user names (e.g. `tddab-plan.md`) and stop.
   Review it with `x-review-plan`, then implement with `x-develop`.

## Output shape (per block -- see tddab-planner.md for the full grammar)

```
<block id="01-order-item">
## TDDAB-1: Order_item value type

<intro>
Immutable value type Order_item{sku, quantity} with value equality.
No dependencies. Creates src/order/order_item.hpp + tests.
</intro>

<red>
- test: Order_item stores sku and quantity
- test: two Order_items with equal fields compare equal
</red>

### Implementation
order_item.hpp -- class with m_sku/m_quantity, ctor takes (std::string sku,
std::uint32_t qty) with std::move, `auto operator<=>(const Order_item&) const = default;`

<success>
- [ ] ctest -R order_item passes
- [ ] cmake --build: 0 warnings
- [ ] clang-tidy clean, ASan/UBSan clean
</success>
</block>
```

## Rules
- **The methodology files are the source of truth** -- do not invent looser/stricter structure
- **No forward dependencies** -- a block never uses a type defined in a later block
- **Each block self-sufficient** -- executable on a clean context from its mission alone
- **Test both paths** of expected/optional; assert throwing paths with `REQUIRE_THROWS_AS`
- **VERIFY includes sanitizers** -- zero warnings, no ASan/UBSan reports
- Plan stays reference-level: code in the plan need not compile, it guides the implementer
