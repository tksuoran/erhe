#!/usr/bin/env python3
"""Proposal D of doc/gl_worker_context_enforcement.md: task SCHEDULING must
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


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    violations = []
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
    if violations:
        print("Task scheduling outside the erhe::task spawn wrappers (proposal A of")
        print("doc/gl_worker_context_enforcement.md). Use erhe::task::spawn /")
        print("spawn_dependent / run / emplace instead:")
        for violation in violations:
            print(f"  {violation}")
        return 1
    print("check_task_spawns: all task scheduling goes through erhe::task wrappers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
