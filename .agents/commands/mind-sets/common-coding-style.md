---
description: "Cross-language coding style rules -- immutability, KISS, DRY, YAGNI, file organization, quality checklist"
---

# Common Coding Style -- All Languages

These rules apply to EVERY language and complement language-specific mindsets.

## Immutability -- CRITICAL

Always create new objects. NEVER mutate existing ones.
- Prevents hidden side effects
- Makes debugging deterministic
- Enables safe concurrency
- Every data transformation: `original -> newObject`

If the language supports it, use: `readonly`, `final`, `const`, `frozen`, `let`, `val`, immutable records.

## Core Principles

### KISS -- Keep It Simple
- Prefer the simplest solution that works
- Avoid premature optimization -- optimize for clarity over cleverness
- If you can't explain it in one sentence, it's too complex
- Three similar lines of code are better than a premature abstraction

### DRY -- Don't Repeat Yourself
- Extract repeated logic into shared functions when the repetition is REAL (3+ occurrences)
- Avoid copy-paste drift -- if you copy, you'll forget to update one copy
- But: don't abstract speculatively. Two similar things are NOT the same thing

### YAGNI -- You Aren't Gonna Need It
- Do not build features before they're needed
- Avoid speculative generality ("we might need this later")
- Start simple, refactor when the pressure is REAL
- Delete dead code -- it's not "preserved for later"

## File Organization
- **Many small files > few large files**
- **200-400 lines** is typical for a well-organized file
- **800 lines** is the absolute maximum -- split if larger
- **50 lines** is the maximum for a single function
- **4 levels** is the maximum nesting depth -- prefer early returns
- Organize by **feature/domain**, not by type (no `utils/`, `helpers/`, `common/`)

## Error Handling -- At Every Level
- Handle errors comprehensively -- never silently swallow
- User-facing code: clear, actionable messages
- Server-side: detailed structured logging
- Fail fast with clear messages at startup for missing config
- Never expose internal details (stack traces, SQL) in API responses

## Input Validation -- At Boundaries Only
- Validate ALL user input at system boundaries (API endpoints, CLI args, file uploads)
- Use schema-based validation (Pydantic, Zod, JSON Schema, etc.)
- Fail fast with clear error messages
- Never trust external data -- parse, don't validate
- Internal code trusts its callers -- don't re-validate inside services

## Naming Conventions (Universal)
- **Descriptive names** -- `getUserById` not `getU` or `fetch`
- **Boolean prefixes** -- `is`, `has`, `should`, `can` (e.g., `isValid`, `hasPermission`)
- **Constants** -- `UPPER_SNAKE_CASE` in most languages
- **No abbreviations** -- `configuration` not `cfg`, `repository` not `repo` (in public APIs)
- **No generic names** -- `data`, `info`, `item`, `thing`, `stuff` are forbidden

## Code Quality Checklist
Before declaring any code "done":
- [ ] Readable names (would a new team member understand?)
- [ ] Functions < 50 lines
- [ ] Files < 800 lines
- [ ] No deep nesting (> 4 levels)
- [ ] Proper error handling at every level
- [ ] No hardcoded values (use constants, config, env vars)
- [ ] No mutation of shared state
- [ ] Input validated at boundaries
- [ ] Tests exist for new logic
