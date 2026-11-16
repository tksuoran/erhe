# Agent orchestration harness

Stability: stable

How a multi-step plan (the USD compatibility plan, a migration series, a
sweep) is worked when one session orchestrates and delegates the coding.
The point is to keep the orchestrator's context small: it holds the plan,
the briefs it wrote, the reviews it did and the decisions it made, and
nothing that an agent can hold instead (source files, build logs, test
output, screenshots it did not need to see).

## Roles

- Orchestrator (the interactive session). Reads the plan and the task
  doc, writes one brief per unit of work, launches one coding agent at a
  time, reviews the resulting diff, commits approved work, keeps the
  memory bank and the queue current.
- Coder (a fresh `Agent` with `model: opus`, `subagent_type:
  general-purpose`; `opus` because the coder makes design decisions inside
  its scope, builds and verifies, and its output is the diff the
  orchestrator reviews). Receives one brief, explores what it needs, edits,
  builds, runs the brief's verification, and reports in the fixed format
  below. It leaves its changes uncommitted.
- Scout (a fresh `Agent` with `subagent_type: Explore`). Answers a
  question the orchestrator needs before writing a brief and returns the
  conclusion only: symbol locations, which call sites exist, whether a
  test already covers something. A scout always runs on a model cheaper
  than the coder's: `model: haiku` for a lookup whose answer is a list of
  locations or a yes / no (where is X declared, which files include Y,
  does a fixture apply schema Z), `model: sonnet` for a question that
  needs the code read and summarized (the shape of a class and how its
  users consume it, how a subsystem's two paths differ). Never `opus`: a
  scout reads and reports, it decides nothing and edits nothing, and its
  report is measured in tens of lines, so the stronger model buys no
  better answer while costing several times more per question. When a
  scout's answer turns out wrong or thin, ask the same scout a narrower
  follow-up or launch a second one; do not promote the scout to `opus`.

A fresh agent gets `CLAUDE.md`, `AGENTS.md` and the memory bank the way
the orchestrator does, so a brief states only what those do not: the
step, its scope, the files and symbols to start from, and the exact
verification recipe. A `fork` agent inherits the orchestrator's whole
context and is used only when the brief would have to restate most of
that context anyway.

## Unit of work

One brief covers one commit: a plan step of size S, or one commit of a
larger step split along the step's own list of what it touches. Coders
that share a checkout run strictly one at a time, because they share its
working tree and build trees; a second coder in the same checkout waits
for the first one's review to close. Units that do not depend on each
other run at the same time in worktrees ("Parallel coders").

## Parallel coders

Independent units of work run at the same time, each coder in its own
git worktree:

- One worktree per parallel coder, beside the main clone: `git worktree
  add ../<name> -b local/<name>` from the current branch's head. The
  branch is local only and is never pushed. A worktree is reused for the
  coder's next unit after its work is integrated (`git reset --hard` to
  the main clone's head, in the worktree only).
- Each worktree has its own build trees and its own `logs/`. It is
  configured with the `CPM_SOURCE_CACHE` environment variable set to the
  main clone's `.cpm_cache`, so the dependencies are not downloaded again.
  The first build of a worktree is a full build.
- Editors of different worktrees run at the same time: an editor whose
  MCP port is taken binds the next free one and logs it, and each run
  reads the port from its own `logs/log.txt`.
- The units are cut so that their files do not overlap. Shared lists
  that every unit would append to (CMake source lists, test tables,
  script tables) are prepared on the main clone first, in a commit of
  its own: the files of every planned unit are created and listed, and
  tables are derived from the code where they can be. Documents that
  every unit adds lines to are written by the orchestrator at
  integration, from the lines each coder puts in its report.
- A coder commits nothing; the review reads the worktree's diff. The
  orchestrator integrates one unit at a time: it commits the approved
  diff on the worktree's branch, cherry-picks that commit onto the main
  clone's branch, builds there and runs the unit's gates again, since
  the units meet for the first time on the main clone.
- At most two coders run at once on a 12-core, 24 GB machine: each
  runs builds and a headless editor.
- When the work is done the worktrees and their branches are removed
  (`git worktree remove`, `git branch -D`).

## Brief contents

1. The step's name and its text from the plan, copied, not paraphrased,
   with the doc path so the agent can read the surrounding sections.
2. Scope: the closed list of what the commit changes (subsystems, files
   or symbols to start from) and what is left to a later commit.
3. Verification recipe: the build script and target, the test binary, and
   the headless MCP steps when the step names them (`erhe-headless-verify`
   skill). The agent runs all of it and reports outcomes verbatim from
   the tool output.
4. Housekeeping the step needs: re-run `scripts\configure_ninja_win_clang.bat`
   after adding a source file; update the owning document under `doc/`
   record; the memory bank is the orchestrator's job.
5. The report format (next section) and the rule that the agent commits
   nothing.

## Coder report format

The report is at most forty lines and holds:

- Files changed (paths only) and a two-sentence summary of the design.
- Verification: each recipe item with its outcome and the relevant
  output lines (test counts, the MCP reply, the log grep hit).
- Proposed commit message (subject and body), ready to use.
- Decisions the agent made where the brief was silent, each in one line.
- Anything unfinished or uncertain, each in one line, or "none".

The agent does not paste diffs, build logs or file contents into the
report; the orchestrator reads the diff itself.

## Review

The orchestrator reviews from the working tree, in this order, and stops
at the first failing gate:

1. `git status --short` and `git diff --stat` match the brief's scope.
2. `git diff` read in full for a diff under about 400 lines; above that,
   per file with `git diff -- <path>`, largest file first. The review
   checks the AGENTS.md rules that agents most often miss (class not
   struct, explicit types, no boolean arguments, no per-frame recompute,
   no allocation in hot paths, no band-aid, ASCII only, comments of one
   line with the explanation in `doc/`) and the step's
   own requirements.
3. One verification claim re-run by the orchestrator when it is the
   riskiest one (a test binary, an MCP query), with output trimmed to the
   lines that matter (`| Select-String`, `tail`).

Fixes go back to the same agent with `SendMessage` so it keeps its
context; a new agent is started only when the first one has clearly lost
the thread. Approved work is committed by the orchestrator with the
agent's proposed message, staging explicit paths and adding the
attribution trailer.

## Context hygiene for the orchestrator

- Read a source file only to settle a review question the diff does not
  answer; use `grep -n` with narrow context, not whole files.
- Build and test output stays in the coder's context; when the
  orchestrator runs a build itself, it filters the output to the error
  lines.
- Screenshots are read by the orchestrator only when the step's
  verification is visual.
- The plan doc is read once per session; later references are to its
  section labels.

## After each commit

The orchestrator updates `memory-bank/activeContext.md` and
`memory-bank/progress.md` for the step, and rewrites the affected plan
statements (status line, the step's verification result) in the present
tense. The queue entry stays a pointer to the plan.
