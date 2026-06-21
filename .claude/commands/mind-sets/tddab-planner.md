# TDDAB Planner Mindset

## What is TDDAB?
**Test Driven Development Atomic Block** - Each block is:
- **Test-First**: Write FAILING tests before implementation
- **Atomic**: Complete, self-contained, independently deployable
- **Block**: Cohesive unit of functionality

### The Three Sacred Phases
```
1. RED Phase    -> Write tests that FAIL
2. GREEN Phase  -> Write code to make tests PASS
3. VERIFY Phase -> Confirm atomic deployment works
```

---

## TDDAB Plan Format

### Required Tags

Plans use lightweight XML tags for machine-parseable structure.
Keep markdown readable -- tags mark boundaries, not replace content.

| Tag | Where | Purpose |
|-----|-------|---------|
| `<mission>` | Once, top of plan | Full project context -- enough that any block can execute on clean context |
| `<block id="NN-name">` | Wraps each TDDAB | Block boundary with unique id |
| `<intro>` | Inside block | Context: what, why, dependencies, files |
| `<red>` | Inside block | Test definitions (bullet list) |
| `<success>` | Inside block | Checklist of verifiable outcomes that must ALL pass |

### Plan Template

```markdown
# TDDAB Plan: [Feature Name]
**Date:** YYYY-MM-DD

<mission>
[Full project context -- architecture, tech stack, patterns, conventions,
file structure, testing approach, build commands. Must contain enough
information that ANY block can be executed on a completely clean context
without prior knowledge. Think of it as the briefing document for a
developer who just walked into the project.]
</mission>

<block id="01-short-name">
## TDDAB-1: [Block Title]

<intro>
[What this block does. Dependencies on previous blocks.
Files to create/modify. Key decisions already made.]
</intro>

<red>
- test: [first test that must fail then pass]
- test: [second test]
- test: [third test]
</red>

### Implementation
[Code in the target language showing types, signatures, key logic, test assertions.
Must be detailed enough that x-develop can produce compilable code from it.
Does NOT need perfect imports or boilerplate -- x-develop handles that.
No TODOs, no unresolved decisions.]

<success>
- [ ] [first verifiable outcome that must pass]
- [ ] [second verifiable outcome that must pass]
- [ ] [third verifiable outcome that must pass]
</success>
</block>

<block id="02-next-thing">
## TDDAB-2: [Next Block]
...same structure...
</block>
```

### Tag Rules
1. `<mission>` -- EXACTLY once, before first block, must be comprehensive enough for clean-context execution
2. `<block id="">` -- id format: `NN-kebab-case`, must be unique across ALL files
3. `<intro>` -- MUST be self-sufficient (executable with zero prior context)
4. `<red>` -- each line starts with `- test:`, describes ONE testable behavior. Must cover ALL cases: happy path, edge cases, error conditions, boundaries. No arbitrary limits on count.
5. `<success>` -- checklist of verifiable outcomes (`- [ ]` format), ALL must pass
6. `<files>` -- ONLY in index.md, signals multi-file mode (see Multi-File Plans below)
7. Tags can span multiple lines
8. No nesting tags inside other tags (except all tags are inside `<block>`)
9. Standard markdown between tags is preserved for human reading
10. **Never write raw TDDAB tag names with angle brackets inside any tag content** (mission, block, intro, red, success, files). This applies to ALL text inside ANY tag -- not just mission. When referencing tags in prose, use backtick-wrapped form or write without angle brackets ("the mission tag", "the files list"). The parser cannot distinguish a literal tag name from a real tag boundary.

---

## WHAT A TDDAB PLAN IS NOT

### NEVER Include:
- Options or alternatives ("Should we A or B?")
- Decisions to be made later
- Discussion or analysis
- "Investigation needed" sections
- "Consider using..." phrases
- Multiple approaches

### NEVER Write:
```
// WRONG - This is discussion, not a plan
"Option A: Use library X"
"Option B: Build custom solution"

// WRONG - This is not executable
"Investigate if we need..."
"Consider whether..."
```

---

## TDDAB Planning Rules

### 1. Information Self-Sufficiency (CRITICAL!)
**Each TDDAB must be understandable with ZERO context:**
- Show FULL file paths, not relative references
- Write code in the target language with types, signatures, key logic, and test assertions
- Code is REFERENCE -- detailed enough for x-develop to produce compilable code, but does NOT need perfect imports or boilerplate
- Never reference "previous discussion" or "as we decided"
- Never use "..." to skip DECISIONS -- but acceptable for standard boilerplate
- `<mission>` must contain enough project context that ANY block works on clean context

**Why:** Plans are executed in fresh context where only one block is visible at a time. The `<mission>` is the only briefing -- it must cover architecture, stack, patterns, commands. The AI agent compiles and verifies during x-develop, not during planning.

### 2. Preliminary Work Handling
**Merge non-testable setup into first TDDAB implementation:**
- Package additions -> Part of TDDAB-1 implementation
- Configuration changes -> Part of implementation phase
- File deletions -> Part of implementation phase

**NEVER create separate "setup" or "preparation" blocks**

### 3. Atomic Block Rules
Each TDDAB must be:
- **Deployable alone** -- System works after this block
- **Rollback-able** -- Can revert with `git revert HEAD`
- **Complete** -- No dependencies on future blocks
- **Tested** -- Has tests that prove it works

### 4. RED = CONTRACT (CRITICAL!)
The `<red>` tests define the **interface contract** for the block -- they specify WHAT the unit must do, not how the full feature works end-to-end.

```
CORRECT: RED tests for a Service block
- test: CreateOrder with valid items -> returns Order with status Created
- test: CreateOrder with empty items -> throws ArgumentException
- test: CreateOrder calls repository.Save exactly once

WRONG: RED tests that are API/E2E tests for the whole endpoint
- test: POST /api/orders returns 201
- test: POST /api/orders with bad data returns 400
(These test the WHOLE stack, not ONE block)
```

### 5. Bottom-Up Decomposition (CRITICAL!)
Features MUST be decomposed into blocks **bottom-up by layer**, not as one monolithic block per endpoint/feature.

```
CORRECT decomposition for "Add Order endpoint":
  Block 01: Order model + validation       (unit tests)
  Block 02: OrderService business logic     (unit tests, mock repository)
  Block 03: OrderRepository persistence     (integration tests)
  Block 04: OrderController endpoint        (tests with mocked service)

WRONG -- one block for entire endpoint:
  Block 01: Complete Order endpoint          (6 API tests)
  (This is NOT atomic, NOT testable in isolation, NOT rollback-safe)
```

**Why bottom-up?**
- Each block tests ONE responsibility in isolation
- If Block 02 fails, you know the problem is in the service, not the model or controller
- Blocks can be developed in parallel by different agents
- Each block is genuinely atomic -- revert one without breaking others

**Decomposition strategy:**
1. **Data/Model** -- entities, DTOs, value objects, validation rules
2. **Logic/Service** -- business rules, orchestration (mock external deps)
3. **Persistence** -- repository, DB access (integration test if needed)
4. **Interface** -- controller/endpoint/CLI (mock service layer)
5. **Wiring** -- DI registration, configuration (final block if needed)

### 6. Test-First Enforcement
```
CORRECT:
1. Write failing test       (RED)
2. Write implementation     (GREEN)
3. Verify tests pass        (VERIFY)
4. Commit and push          (COMMIT)

WRONG:
1. Write implementation
2. Write tests afterward
```

---

## TDDAB Size Guidelines

### Ideal TDDAB Size:
- **Tests**: ALL necessary -- happy path, edge cases, error conditions, boundary values. No fixed number. A simple validator may need 2 tests, a complex service may need 15. Cover ALL edge cases.
- **Files**: 1-3 files modified
- **Scope**: Single layer or single responsibility

### Too Large (Split It):
- Modifies > 5 files
- Multiple unrelated features
- Can't deploy independently
- Spans multiple layers (model + service + controller in ONE block)
- Tests are API/E2E level instead of unit level

### Too Small (Merge It):
- Single test case
- < 10 lines of code
- Just config changes

---

## TDDAB Naming Convention

```
<block id="NN-kebab-case-name">
## TDDAB-N: [Verb] [Feature] [Context]
```

Examples:
```
<block id="01-add-auth-config">
## TDDAB-1: Add Authentication Configuration

<block id="02-replace-jwt-oauth">
## TDDAB-2: Replace JWT with OAuth Provider

<block id="03-update-claims-logic">
## TDDAB-3: Update Claims Extraction Logic
```

id matches the block number and a short descriptive slug.

---

## Execution Order Section

Every plan MUST end with an execution order showing dependencies:

```markdown
## Execution Order
01-first-thing    -> no dependencies
02-second-thing   -> depends on 01
03-third-thing    -> depends on 01, 02
04-parallel-thing -> depends on 01 (can run parallel with 02-03)
```

---

## TDDAB Quality Checklist

For each block, verify:
- [ ] Has unique `<block id="">`
- [ ] Has `<intro>` with full context
- [ ] Has `<red>` with testable behaviors
- [ ] Has `<success>` with clear exit criterion
- [ ] Tests are written first and will fail
- [ ] Implementation approach is clear (no TODOs, no unresolved decisions)
- [ ] Block is atomic and deployable
- [ ] No decisions or options included
- [ ] File paths are exact
- [ ] Code shows types, signatures, key logic, assertions (compilability verified by x-develop)
- [ ] Can be rolled back independently
- [ ] No dependencies on future blocks

---

## Multi-File Plans

For large plans, split into an **index.md** + multiple sub-files to avoid mission duplication.

### When to Split
- Plan exceeds ~300 lines or ~10 blocks
- Natural layer boundaries exist (models, services, API, UI)
- Multiple developers/agents will work on different sections

### index.md Format
```markdown
# TDDAB Plan: [Feature Name]
**Date:** YYYY-MM-DD

<mission>
[Full project context -- single source of truth]
</mission>

<files>
- 01-models.md
- 02-services.md
- 03-api.md
</files>
```

### Sub-file Format (e.g., 01-models.md)
```markdown
# Models Layer

<block id="01-order-entity">
## TDDAB-1: Create Order Entity

<intro>
[Context for this block]
</intro>

<red>
- test: [first test]
</red>

### Implementation
[Reference code]

<success>
- [ ] [verifiable outcome]
</success>
</block>
```

### Multi-File Rules
1. `<mission>` in index.md is **authoritative**. `<mission>` in sub-files is silently ignored.
2. `<files>` tag in index.md signals multi-file mode. One filename per line, prefixed with `- `.
3. Block IDs must be **globally unique** across all files.
4. File order in `<files>` = execution order.
5. Paths are relative to the directory where index.md lives.
6. No `<files>` tag + `<mission>` present = single-file plan (full backward compatibility).

---

## CVM Integration

This plan format is designed to be parsed by CVM:
- `<mission>` -> initial context prompt
- `<block>` -> task array entry with id, line references
- `<intro>` + `<red>` -> prompt for RED phase
- `<success>` -> prompt for VERIFY loop exit criteria
- `<files>` -> multi-file mode: CVM reads index.md for mission, then parses each sub-file for blocks

When NOT using CVM, the same format works perfectly for manual execution -- tags are lightweight and don't hurt readability.

---

## GOLDEN RULES OF TDDAB

1. **If you write "Option A or B"** -> STOP, make the decision first
2. **If you write "TODO"** -> STOP, make the decision now
3. **If you use "..." to skip a decision** -> STOP, resolve it ("..." for boilerplate is fine)
4. **If you write "investigate"** -> STOP, do it now, then plan
4. **If tests aren't first** -> STOP, restructure the block
5. **If it's not atomic** -> STOP, split or merge blocks
6. **If there's no `<block>` tag** -> STOP, add structure

---

## ACTIVATION TRIGGER

When user requests TDDAB planning:
1. Ensure all decisions are made first
2. Create atomic blocks with clear reference code
3. Tests ALWAYS come first
4. Use `<mission>`, `<block>`, `<intro>`, `<red>`, `<success>` tags
5. No options, no discussions, no investigations
6. Complete, deployable code only
7. End with execution order

**A TDDAB plan is a RECIPE, not a DISCUSSION!**
