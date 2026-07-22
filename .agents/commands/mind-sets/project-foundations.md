---
description: "Project Foundations - Principles that prevent codebase deterioration"
---

# Project Foundations

Principles that prevent codebase deterioration. Apply ALL of these from Day 1 on new projects. Verify ALL of these during audits on existing projects.

**The cost of adding these on Day 1**: Zero.
**The cost of adding these later**: Massive refactoring, version conflicts, hundreds of warnings to fix, silent bugs discovered months late.

---

## 1. Zero-Tolerance Warnings

**Principle**: Warnings are errors. The build is GREEN (0 warnings, 0 errors) or RED. Nothing in between.

**Why**: One warning today becomes 50 next week. Nobody reads them anymore. Nullable warnings, unused variables, deprecated APIs -- all caught at compile time instead of production.

**Without this**: Warnings accumulate silently. Real problems hide in the noise. Adding strict warning-as-error settings to a large codebase means hundreds of fixes.

---

## 2. Central Dependency Management

**Principle**: ONE place defines ALL dependency versions for the entire project/solution. Every module uses the same version of every dependency.

**Why**: Without central management, Module A uses v13.0.1 and Module B uses v13.0.3. They conflict at runtime. Upgrading is a hunt across dozens of files.

**Without this**: Version drift, transitive dependency conflicts, "works on my machine" bugs.

---

## 3. Versioning Strategy

**Principle**: Every build has a unique version. The running application reports its version. You can verify after deploy that the correct code is running.

**Why**: Without versioning, you build new code but test old code without knowing. Debug traces without version are useless -- you don't know which code produced the log.

**Without this**: "I deployed the fix" -- but the old binary is still running.

---

## 4. Structured Error Handling

**Principle**: Errors have typed codes, not opaque strings. Consumers can react programmatically. Errors include context (suggestions, candidates, related data).

**Why**: `throw new Exception("something went wrong")` tells you nothing. Structured errors enable programmatic handling, consistent formatting, and actionable messages.

**Without this**: String parsing to understand errors, inconsistent messages, no programmatic error handling.

---

## 5. Test Configuration (Fast/Slow Split)

**Principle**: Fast tests run by default in seconds. Slow tests (integration, E2E, external services) run on demand. One slow test must NOT block the entire suite.

**Why**: If running tests takes 15 minutes, developers stop running them. Tag slow tests, exclude by default, run explicitly when needed.

**Without this**: Test suite gets slow, developers skip tests, bugs slip through.

---

## 6. Immutable DTOs

**Principle**: Data transfer objects are immutable after creation. No accidental mutation. Value equality by default. Thread-safe by construction.

**Why**: Mutable DTOs lead to "who changed this field?" bugs. Immutability means no accidental side effects, straightforward testing, and no locking needed.

**Without this**: Subtle mutation bugs, defensive copying everywhere, threading issues.

---

## 7. Dependency Injection with Proper Registration

**Principle**: Every library/feature exposes its own registration method. The composition root is a clean sequence of registration calls -- readable in 10 seconds. Dependencies between features are explicit.

**Why**: Without this, the composition root becomes a 500-line mess. Nobody knows which registrations belong to which feature. Removing a feature means hunting across the file.

**Without this**: Spaghetti composition root, implicit dependencies, impossible to understand what's registered and why.

---

## 8. Centralized Configuration with Validation

**Principle**: ALL configurable values live in configuration files. NO hardcoded defaults in code. If a required value is missing, the app fails at startup with a clear message.

**Why**: Hidden defaults are time bombs. If a path defaults to `"./data"` and someone runs from a different directory, files go to the wrong place silently.

**Without this**: Silent misconfiguration discovered in production. "It works locally but not on the server."

---

## 9. Structured File Logging

**Principle**: Logs go to FILES, not console. Use structured logging through abstractions. Only the host/entry point references the logging implementation. Libraries use abstractions only. Log levels and sinks are in configuration -- changeable without recompilation.

**Why**: Console output interferes with STDIO transports, gets lost in containers, and is not searchable. File logs persist, are searchable, and can be shipped to monitoring systems.

**Without this**: Lost logs, no debugging capability in production, logging coupled to implementation.

---

## 10. Zero-Dependency Core Module

**Principle**: Every solution has a core module containing ALL contracts (interfaces, DTOs, enums). Core has zero external dependencies -- only language standard library and framework abstractions. Every other module references Core.

**Why**: If Core has dependencies, those leak into every plugin, every test, every consumer. When you replace an implementation, Core is untouched -- contracts don't change.

**Without this**: Everything depends on everything. Replacing one library means touching every module.

---

## 11. Interface-First Design

**Principle**: Every significant component exposes an interface. The interface IS the boundary. Implementation is behind the interface.

**Why**: Without interfaces: you can't mock (can't test in isolation), you can't swap implementations (can't evolve), dependencies are implicit (refactoring is dangerous).

**Without this**: Tightly coupled code, untestable components, risky refactoring.

---

## 12. Internal/Private by Default

**Principle**: Only make public what MUST be public. Every public type/method is a commitment -- once public, changing it breaks consumers. Implementation details stay hidden.

**Why**: Small public surface = freedom to refactor internals without breaking contracts. Large public surface = every change is a breaking change.

**Without this**: Consumers depend on implementation details. Refactoring becomes impossible without breaking changes.

---

## 13. Test Module per Production Module

**Principle**: Each production module has exactly one test module. A failure tells you WHICH module is broken. Every code path is tested.

**Why**: If all tests are in one project, a failure could be anywhere. With test isolation per module, you know exactly where to look.

**Without this**: Test failures don't tell you where the problem is. Modules ship with untested code paths.

---

## 14. Convention Over Configuration

**Principle**: Use naming conventions and directory structure to drive discovery and wiring. Adding a new component should require zero manual registration when possible.

**Why**: Manual registration means adding a new component requires modifying existing code. Convention-based discovery means drop a file in the right place and it works.

**Without this**: Every new component requires touching multiple files. Forgot to register? Silent failure.

---

## 15. Deterministic Build Output

**Principle**: Build output goes to a known, predictable location. No stale artifacts confusion between environments. Clean builds are reproducible.

**Why**: If build output is scattered or unpredictable, you test stale artifacts. Cross-platform development amplifies the problem.

**Without this**: "It compiled but the old DLL/JAR/bundle is still there." Stale build artifacts cause phantom bugs.

---

## 16. Black Box Composition

**Principle**: Decompose the system into self-contained black boxes. Each has a clear interface, hidden internals, and isolated dependencies. Black boxes can't break each other.

**Why**: Without isolation, changes ripple unpredictably across the system. With black boxes, each component is independently testable, replaceable, and understandable.

**Without this**: Changes ripple unpredictably. One "small fix" breaks three other modules. Nobody dares refactor because the blast radius is unknown. Interface is sacred (breaking it breaks consumers). Scope discipline (work on one box at a time). Test in isolation (mock dependencies).

---

## For Audits

When auditing an existing project, verify each principle:
- **Present**: The principle is applied correctly
- **Partial**: Partially applied or inconsistently applied
- **Missing**: Not applied -- flag as finding with severity

Missing Day-1 principles (1-6) on a mature project are the most expensive to fix. Flag them as high severity.
