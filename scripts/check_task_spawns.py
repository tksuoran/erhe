#!/usr/bin/env python3
"""Proposal D of doc/erhe/gl_worker_context_enforcement.md: task SCHEDULING must
go through the erhe::task spawn wrappers, which assert that the calling
thread holds no worker GL context. This is a grep-level rule; without it the
spawn-site guard (proposal A) decays from construction into documentation.

Flagged forms (the scheduling vocabulary):
  - silent_async(            schedules immediately
  - silent_dependent_async(  schedules when predecessors finish
  - executor.run( / executor->run(   the schedule point of a built taskflow
  - subflow.emplace( / subflow->emplace(   proxy for the join that schedules

tf::Taskflow::emplace is deliberately NOT flagged: graph construction
schedules nothing; run() is the schedule point and is flagged.

A tf::Executor is constructed only by erhe::task::make_executor, which names
and prioritizes its workers (D2.7 of doc/plans/worker_executors.md). Flagged
construction forms:
  - tf::Executor name; / name(...) / name{...} / name = ...   object or member
  - tf::Executor(...) / tf::Executor{...}                     temporary
  - make_unique<tf::Executor> / make_shared<tf::Executor>
  - new tf::Executor
References, pointers, std::unique_ptr<tf::Executor> and forward declarations
are not flagged.

Scanned: src/editor and src/erhe (the trees that share a graphics device).
Exempt: src/erhe/task (the wrapper itself) and test directories (tests run
their own executors with no shared graphics device). geogram_soak and the
standalone apps are outside the scanned trees; they have no shared device.
"""
import re
import sys
from pathlib import Path

SCAN_ROOTS = ("src/editor", "src/erhe")
EXEMPT = ("src/erhe/task/",)

PATTERNS = (
    re.compile(r"\bsilent_async\s*\("),
    re.compile(r"\bsilent_dependent_async\s*\("),
    re.compile(r"\bexecutor\s*(\.|->)\s*run\s*\("),
    re.compile(r"\bsubflow\s*(\.|->)\s*emplace\s*\("),
)

EXECUTOR_PATTERNS = (
    re.compile(r"\btf::Executor\s+[A-Za-z_]\w*\s*[;({=]"),
    re.compile(r"\btf::Executor\s*[({]"),
    re.compile(r"\bmake_(unique|shared)\s*<\s*tf::Executor\s*>"),
    re.compile(r"\bnew\s+tf::Executor\b"),
)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    violations = []
    executor_violations = []
    for scan_root in SCAN_ROOTS:
        for path in sorted((repo_root / scan_root).rglob("*")):
            if path.suffix not in (".cpp", ".hpp", ".h", ".inl"):
                continue
            relative = path.relative_to(repo_root).as_posix()
            if any(relative.startswith(exempt) for exempt in EXEMPT):
                continue
            if "/test/" in relative:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            in_block_comment = False
            for line_number, line in enumerate(text.splitlines(), start=1):
                code = line
                if in_block_comment:
                    end = code.find("*/")
                    if end < 0:
                        continue
                    code = code[end + 2:]
                    in_block_comment = False
                code = code.split("//", 1)[0]
                start = code.find("/*")
                while start >= 0:
                    end = code.find("*/", start + 2)
                    if end < 0:
                        code = code[:start]
                        in_block_comment = True
                        break
                    code = code[:start] + code[end + 2:]
                    start = code.find("/*")
                for pattern in PATTERNS:
                    if pattern.search(code):
                        violations.append(f"{relative}:{line_number}: {line.strip()}")
                for pattern in EXECUTOR_PATTERNS:
                    if pattern.search(code):
                        executor_violations.append(f"{relative}:{line_number}: {line.strip()}")
                        break
    if violations:
        print("Task scheduling outside the erhe::task spawn wrappers (proposal A of")
        print("doc/erhe/gl_worker_context_enforcement.md). Use erhe::task::spawn /")
        print("spawn_dependent / run / emplace instead:")
        for violation in violations:
            print(f"  {violation}")
    if executor_violations:
        print("tf::Executor constructed outside erhe::task::make_executor (D2.7 of")
        print("doc/plans/worker_executors.md):")
        for violation in executor_violations:
            print(f"  {violation}")
    if violations or executor_violations:
        return 1
    print("check_task_spawns: all task scheduling goes through erhe::task wrappers")
    print("check_task_spawns: every tf::Executor is made by erhe::task::make_executor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
