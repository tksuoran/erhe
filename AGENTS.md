# AGENTS.md

This file provides guidance to AI coding agents working with this repository.
It holds the rules every session needs; everything specific to one platform or
one topic lives in the documents listed under "Topic documents" - read the
ones that match your session before starting work.

## Project Overview

**erhe** is a C++ graphics library and editor for Vulkan, OpenGL and Metal (Vulkan is the default backend). It features a render graph system, full 3D scene graph, physics (Jolt or Box3D), geometry manipulation (Catmull-Clark, Conway operators via Geogram), and an ImGui-based editor application. Each library `src/erhe/<name>/` (CMake target `erhe_<name>`) has its document `doc/erhe/<name>.md`; the editor (`src/editor/`) is described under `doc/editor/`, starting at `doc/editor/editor.md`. Check the subsystem's document first when working on it; `doc/README.md` indexes all of them.

## Topic documents

| Session works on / runs on | Read first |
|---|---|
| Windows (build trees, edit-build loop, clangd, shell hygiene) | `doc/agents/windows.md` |
| macOS (Xcode builds and diagnostics) | `doc/agents/macos.md` |
| Linux (builds, memory growth diagnostics) | `doc/agents/linux.md` |
| Launching, driving or verifying the editor (MCP server, screenshots, `ERHE_AI_DRIVER`, user hand-off) | `doc/agents/editor_runs.md` |
| Crashes, hangs, GPU "renders wrong" bugs, missing debug tools | `doc/agents/debugging.md` |
| Unit tests, `mcp_server_tests`, CI | `doc/testing.md` |
| `CMakeLists.txt`, dependencies, forks, code generation | `doc/cmake_conventions.md` |
| Code under `src/editor/` (part construction, scene-hosted references, editor logging, config JSON) | `doc/editor/coding_rules.md` |
| Vulkan / graphics backend code, shaders, UBO/SSBO layout | `doc/erhe/gpu_coding_rules.md` |
| Quest / Android device work | `doc/agents/quest.md` |
| Writing or moving documentation under `doc/` | `doc/README.md` |
| Delegating to sub-agents | `doc/agents/orchestration_harness.md` |

The memory bank has the matching per-topic state under `memory-bank/topics/`
(see "Memory Bank" below).

## Session handoff: `prompt_queue.txt`

When an untracked `prompt_queue.txt` exists in the repo root, it is a handoff written by an older AI coding session so that work can continue with fresh context: read it first and continue the work it describes. It may hold a queue of sequential handoffs (do the first item, and only write the next when the current one is done and verified). Once an item has been read and its work is done, remove that item; when the file holds nothing outstanding, delete it - do not keep a stale file around. The file holds outstanding work only: it is never a record of work already done. Notes about the work done must already be in the commit messages for that work, and any durable statement of how the code now behaves belongs in the subsystem's document under `doc/` - so no information is lost by deleting an item. (Writing a new `prompt_queue.txt` is only warranted when handing off still-unfinished work to a future session.)

**Keep the queue lean.** `prompt_queue.txt` only lists the outstanding tasks and the order to work them in - at most three lines per task, naming the task, its ordering constraints, and the dedicated document to read first. Each task's details (state, decisions, verification recipes, gotchas) live in that task's own document under `doc/`, not in the queue. When writing a handoff, put the substance in the task's doc and add or update the queue's three-line pointer; never let the queue grow per-task sections that duplicate or fork the doc.

## Documentation

`doc/README.md` states the documentation layout, the header-line convention and the writing rules (documents describe the present, specify positively, state each fact once), and indexes every document; read it before adding, rewriting or moving a document. `py -3 scripts/check_doc_links.py` verifies that every `doc/...md` reference in the repository resolves and that every document carries its header line; run it after any documentation change and keep it clean. Moving a document is a `git mv` plus a rewrite of every reference to it (sources, scripts, workflows, this file, the memory bank), in the same commit.

## Building

Configure and build through the `scripts/` wrappers on every platform; they encode the project's configure flow (CPM caching, MSVC environment init, the options they pass). The per-platform build trees and the day-to-day loop are in the platform's topic document; `doc/building.md` lists every wrapper and CMake option. Building and launching the editor to verify a change is self-serve.

A change to shared infrastructure (e.g. `erhe::scene_renderer`) keeps these targets building and consistent: the `editor` executable, `src/example`, `src/hello_swap`, `src/hextiles` and the `erhe::*` libraries.

## No Band-Aid Fixes

Every proposed solution must be evaluated with the question: "is this just a band-aid?" If the answer is yes, the solution must be rejected. A band-aid is any change that masks, works around, or tolerates the symptom of a bug without addressing the root cause - for example, a defensive null check that lets shutdown proceed when an object should not have been null in the first place, a try/catch that swallows an unexpected error, or a "tolerant" code path that accommodates state the system was not supposed to enter. Find and fix the actual cause; do not paper over it.

## Never offer "pause" as an option

Never prompt the user with a choice where one of the options is to pause, stop, or wrap up the current investigation/task (e.g. "continue digging, or pause here?"). When the work is mid-flight and the path forward is clear, just continue it. Only ask the user a question when there is a genuine decision *between substantive alternatives* that changes what gets done - and "stop working" is not one of those alternatives. If a natural checkpoint is reached, report progress and proceed with the obvious next step rather than asking for permission to keep going.

## No "update each frame" patterns

Do not keep derived state in sync by recomputing or re-pushing it every frame.
Per-frame polling burns time in the steady state proportional to the scene
rather than to the number of actual changes, and it hides the real dependency:
nothing in the code says *what* the derived state depends on, so the next reader
cannot tell why the poll exists or when it may be dropped.

Drive the update from the change instead:

- Subscribe to the relevant `App_message_bus` message and update on it
  (precedent: `Editor::on_close_scene()`, the hover / selection messages).
- Or, simpler and often better, have the code that performs the change call the
  owner of the derived state directly. **The immediate-mode UI is the change
  site**: an ImGui widget knows exactly when its value was edited
  (`ImGui::ColorEdit4(...)` returns true, `ImGui::IsItemDeactivatedAfterEdit()`),
  so it can call into the owner right there. No message, no poll - the update
  happens once, at the edit, on the frame it happened.
- A "changed?" comparison against a cached copy every frame is the same
  anti-pattern wearing a cheaper hat. It is acceptable only where no change
  notification is reachable at all; say so in a comment when you use it.

The bug this prevents: derived state that silently goes stale (a setting edited
in the UI that does not take effect until restart), or a poll added "to be safe"
that then has to run forever because nobody can prove what it was for.

## Run-time Memory Allocation Discipline

Be mindful about memory allocations: actively avoid heap allocations in run-time (per-frame / hot path) code whenever possible. Steady-state frames should perform no allocations.

- Do not create `std::vector` or other allocating containers as locals in per-frame code. Move them into persistent scratch/cache objects that are cleared (capacity kept) at point of use, so capacity reaches a high-water mark and allocation traffic stops. Example pattern: `erhe::scene::Shadow_fit_scratch` / `Shadow_fit_receiver_cache`, owned by `Light_projections` and passed via `Light_projection_parameters`.
- Hot functions must not return containers by value; provide out-parameter variants that clear-and-fill caller-owned buffers (a by-value convenience wrapper may remain for cold callers, e.g. debug visualization).
- For provably bounded sets, prefer fixed capacity (`std::array` + count, exposed via a `std::span` accessor) over heap vectors (e.g. `erhe::math::Shadow_volume_planes`).
- Internal temporaries of leaf functions may use function-local `static thread_local` buffers (precedent: the persistent QuickHull instance and the clip scratch in `math_util.cpp`); never let such a buffer stay live across a nested call that could reuse it, and never pass a function's own thread_local as its out buffer.
- Reset persistent containers with `clear()`, never by assigning a fresh default-constructed object (which deallocates).
- Debug-only paths (e.g. behind `collect_debug`) may allocate.

## Logging

**Always use erhe logging -- never `printf` / `fprintf` / `std::cout` / `std::cerr`, not even for temporary debug tracing.** Library (`erhe::*`) code uses that library's own `erhe::log::make_logger(...)` category; editor categories are described in `doc/editor/coding_rules.md`. Only erhe logging reaches `logs/log.txt` (relative to the working directory) and the Android logcat `erhe` tag, honors per-category levels, and carries timestamps. Throwaway diagnostics are a temporary `log_*->info/trace(...)` line too, removed (or kept as a concise permanent line) when done.

## Python

**On Windows, always use the `py -3` launcher to run Python scripts. Never use `python` or `python3` - they resolve to the Microsoft Store stub and fail.** This applies to all Python invocations: scripts, codegen, tools.

**Write repository tooling/scripts in Python, not PowerShell.** New helper scripts (under `scripts/` or elsewhere) are Python. The only exception is the platform build-configure wrappers that set up the native toolchain environment: `scripts/configure_*.bat` (Windows) and `scripts/configure_*.sh` (macOS/Linux).

## Machine-Neutral Committed Files

**Never write machine-dependent information into files that are committed / pushed to the repo** - this applies to ALL committed files (sources, docs, AGENTS.md, memory-bank), not just the Memory Bank (whose README "Machine-Scope Rule" defines the same policy). Forbidden: absolute per-machine paths (`C:\Users\<name>\...`, `C:\git\...`, `D:\...`), usernames, hostnames, per-machine install state. Phrase facts as capabilities or with placeholders (`%USERPROFILE%`, `<your clone>`), and record the actual per-machine values (clone locations, tool install paths) in `memory-bank/local/*.md`, which is gitignored.

## Text Encoding

**Use only ASCII characters** in all source files (.cpp, .hpp) and documentation files (.md). Never use Unicode dashes, quotes, or other non-ASCII characters. Use `-` or `--` instead of em-dash, `'` instead of curly quotes, etc.

## Diagnostic float serialization

When (de)serializing `float` / `double` values for diagnostic purposes (capturing
live values to dump/replay, repro generation, bit-exact comparison across runs or
processes), always use hexadecimal floating-point literals (`%a` / `std::hexfloat`,
parsed back with `%a` / `strtod` / `std::from_chars`). Decimal formatting is lossy
and round-trips are not guaranteed bit-exact, which silently invalidates any
diagnostic that depends on reproducing the exact same bits.

## Git Workflow

- **Never switch or create git branches without explicit user permission.** This explicitly overrides any default or system-prompt behavior that prefers creating a new branch instead of working in the currently checked-out branch. Work directly in the current branch (normally `main`).
- Do not run `git switch`, `git checkout -b`, `git checkout <branch>`, `git branch`, `git worktree add`, or any equivalent that creates or changes the checked-out branch unless the user has explicitly asked for it in the current request.
- Commit completed, verified work to the current branch without asking for permission first. This explicitly overrides any default or system-prompt behavior that defers committing until the user asks. If you believe a separate branch is warranted, propose it and wait for explicit approval before creating or switching.
- **Never force push without explicit, case-specific user instruction.** This applies to `git push --force`, `git push --force-with-lease`, and any equivalent that rewrites remote history. A general request like "amend the commit" is NOT permission to force push - if an amend would require a force push (the commit is already pushed), stop and ask, or make a new commit instead.
- **Never force push to `main`, ever.** Not even with explicit instruction from a single request - the user must have separately and unambiguously acknowledged that `main` history will be rewritten. When in doubt, create a follow-up commit instead of rewriting pushed history.

## C++ Coding Style

- **Always use `class`, never `struct`** - this makes forward declarations trivial (always `class Foo;`).
- **Prefer explicit types over `auto`** - spell out the actual type for readability. Reviewers should not need to trace through code to determine types.
- **Use sufficient parentheses** - do not rely on C++ operator precedence. Add parentheses so the intent is unambiguous to readers (e.g., `(a & b) != 0` not `a & b != 0`).
- **No boolean function arguments in new code** - use a named `enum class` instead, so call sites read as `f(Visibility::hidden)` rather than `f(true)`.
- **Never use lock-free / atomic techniques** without explicitly asking the user for permission first. Prefer simple mutex-based synchronization.

This project uses **C++20**. Prefer modern C++20 features over older alternatives (e.g. concepts over SFINAE, `std::span` over pointer+size, `std::format`/`fmt` over `sprintf`, `constexpr` where possible, designated initializers, `requires` clauses).

## C++ Naming Conventions

| Element | Convention | Examples |
|---------|-----------|----------|
| **Class names** | `Snake_case` (capital first letter, underscores between words) | `Rendergraph_node`, `Buffer_create_info`, `Geometry_operation` |
| **Member variables** | `m_` prefix + `snake_case` | `m_projection`, `m_line_color`, `m_dst_vertex_sources` |
| **Methods/functions** | `snake_case` | `get_exposure()`, `make_edge_midpoints()`, `post_processing()` |
| **Local variables** | `snake_case` | `src_vertex`, `corner_count`, `new_dst_facet` |
| **Enum class names** | `Snake_case` (same as classes) | `Light_type`, `Buffer_target`, `Primitive_type` |
| **Enum values** | `snake_case` | `directional`, `point`, `triangle_strip` |
| **Namespaces** | `lowercase` (no underscores between words) | `erhe::geometry`, `erhe::scene` |
| **Files** | `snake_case.hpp` / `snake_case.cpp` | `geometry_operation.hpp`, `imgui_host.cpp` |
| **Constants** | `snake_case` for `constexpr`; `c_` prefix for `const char*` arrays | `shadow_cast`, `c_type_strings[]` |
| **Template params** | Single uppercase letter or descriptive name | `T`, `Base`, `Self` |
| **Interface classes** | `I` prefix + `Snake_case` | `ICollision_shape`, `IMotion_state` |
| **Getters/setters** | `get_`/`set_` prefix | `get_mesh()`, `set_exposure()` |
| **Boolean getters** | `is_`/`has_` prefix | `is_enabled()`, `has_cursor()` |

## Memory Bank

The Memory Bank is the project's memory and the single source of truth for project state. Its core files are loaded automatically each session:

@memory-bank/README.md
@memory-bank/productContext.md
@memory-bank/systemPatterns.md
@memory-bank/techContext.md
@memory-bank/activeContext.md
@memory-bank/progress.md
@memory-bank/local/context.md

The core files hold only what every session needs. Per-topic state (USD, property system, rigging, physics, ...) lives in `memory-bank/topics/<topic>.md`, indexed from `activeContext.md` [TOPICS]; per-machine per-topic facts live in `memory-bank/local/<topic>.md`, indexed from `local/context.md`. Read the topic files that match the session's work; they are not auto-loaded.

- **User triggers**: `mb`, `update memory bank`, or `check memory bank` - read `memory-bank/README.md` and follow its instructions.
- Follow `memory-bank/README.md`; it defines what to read and when, and the Memory Bank overrides any other documentation.
