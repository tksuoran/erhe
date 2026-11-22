# Memory bank

What the repository's documents do not hold: traps, decisions and measured
results a later session needs about a subject, and facts about one machine.

## What goes where

- `topics/<topic>.md`: working notes about one subject that no document
  under `doc/` has a place for. Read the topic file that matches the
  session's work before starting it; they are not loaded automatically.
  The index is in `AGENTS.md` "Memory Bank". A new topic file gets a row
  there; a topic file with no entry left is deleted.
- `local/context.md` and `local/<topic>.md`: facts about this machine (clone
  locations, tool install paths, per-machine baselines). The directory is
  gitignored. `local/context.md` is loaded automatically and indexes the
  other local files.

Kept elsewhere, and never duplicated here:

- outstanding tasks and their order: `doc/plans/open_items.md` "Work order";
- the state of a task (steps, what is built, open points, user checks): the
  task's own document under `doc/`;
- work done: the commit messages;
- how the code behaves: the subsystem's document under `doc/`;
- rules: `AGENTS.md` and `doc/agents/`.

## Writing an entry

- First look for the document that owns the fact (the subsystem's
  document, its plan, `doc/agents/`, `doc/testing.md`,
  `doc/plans/open_items.md`) and write it there. An entry goes into a
  topic file only when no document does: the user's preferences for a
  kind of work, a debugging or agent-workflow trap, a measurement
  baseline.
- Plain English in short bullets, ASCII only, under a heading that names
  the subject; the writing rules of `doc/README.md` apply (present tense,
  each fact once, state what to do).
- Write what a later session cannot get from the code, the documents or
  `git log`: a trap that was hit and the rule that avoids it, a decision of
  the user with the user's words and date, a measured result needed to read
  a later re-run.
- Update or remove an entry that has stopped being true; an entry that only
  records finished work is removed, since the commit log has it.
- A user trigger `mb`, `update memory bank` or `check memory bank` means:
  read the topic files of the session's work and bring them up to date by
  these rules.

## Machine-neutral rule

Everything committed is shared by every machine and is public
(`AGENTS.md` "Machine-Neutral Committed Files"). Topic files state
capabilities ("needs clangd >= 22"), use placeholders for paths
(`%USERPROFILE%`, `<your clone>`) and credit public identities only. Per
machine facts go under `local/`. Secrets, tokens and keys are never written
anywhere, and neither is their location.

## Reading the older local entries

The `local/` files written before 2026-11-22 use a compressed notation
(MBEL): hyphen-joined words with operators. `>` past, `@` present, `?`
todo, `!` important, `::` is, `{}` details, `()` note, `|` or, `+` and,
`#` count; a later line overrides an earlier one on the same subject.
Rewrite an entry in plain English when the session works on its subject.
