#!/usr/bin/env python3
"""Check the documentation tree: every reference to a doc resolves, and every
document under doc/ carries its stability or status line.

Rules checked (see doc/README.md for the conventions they enforce):

1. Every `doc/<path>.md` / `doc/<path>.json` mention anywhere in the repository
   text (sources, scripts, CMake files, Markdown, workflows, AGENTS.md) names
   an existing file.
2. Every relative Markdown link `[text](target)` inside a `.md` file resolves
   relative to that file (URLs and anchors-only links are skipped).
3. No file mentions a `notes.md` path: library and editor notes live under
   doc/ as doc/erhe/<name>.md / doc/editor/<subdir>.md.
4. Every `.md` under doc/ (except doc/reference/, doc/gltf_extensions/schema/)
   has, within its first ten lines, exactly one of:
       Stability: stable | mostly stable | experimental   (doc/*.md, doc/erhe/, doc/editor/, doc/<topic>/)
       Status: proposed | in progress | blocked           (doc/plans/**)

Exit code 0 when clean, 1 when any check fails. Run from anywhere:

    py -3 scripts/check_doc_links.py
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

SCAN_ROOTS = ["src/erhe", "src/editor", "src/example", "src/hello_swap", "src/hextiles", "src/CMakeLists.txt", "scripts", "doc", ".github", "cmake", "AGENTS.md", "CLAUDE.md", "Readme.md", "CMakeLists.txt", "prompt_queue.txt"]
SCAN_SUFFIXES = {".cpp", ".hpp", ".h", ".c", ".mm", ".py", ".md", ".txt", ".cmake", ".yml", ".yaml", ".bat", ".sh", ".json", ".glsl", ".frag", ".vert", ".comp"}
SKIP_DIR_NAMES = {".git", ".cpm_cache", "node_modules", "__pycache__"}
SKIP_FILE_PREFIXES = ("wuffs-",)  # vendored single-file libraries
SKIP_DIR_PREFIXES = ("build_",)

DOC_PATH_RE = re.compile(r"\bdoc/[A-Za-z0-9_][A-Za-z0-9_./-]*\.(?:md|json)\b")
MD_LINK_RE = re.compile(r"\]\(([^)\s]+)\)")
NOTES_RE = re.compile(r"(?<![A-Za-z0-9_-])(?:[A-Za-z0-9_./-]*/)?notes\.md\b")

STABILITY_RE = re.compile(r"^Stability: (stable|mostly stable|experimental)\s*$")
STATUS_RE = re.compile(r"^Status: (proposed|in progress|blocked)\s*$")

HEADER_EXEMPT_DIRS = ("doc/reference/", "doc/gltf_extensions/schema/")


def iter_scan_files() -> list[Path]:
    files: list[Path] = []
    for root_name in SCAN_ROOTS:
        root = REPO_ROOT / root_name
        if root.is_file():
            files.append(root)
            continue
        if not root.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_NAMES and not d.startswith(SKIP_DIR_PREFIXES)]
            for filename in filenames:
                path = Path(dirpath) / filename
                if filename.startswith(SKIP_FILE_PREFIXES):
                    continue
                if path.suffix.lower() in SCAN_SUFFIXES or filename == "CMakeLists.txt":
                    files.append(path)
    return files


def rel(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def check_file(path: Path, problems: list[str], notes_ok: bool) -> None:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        problems.append(f"{rel(path)}: cannot read ({error})")
        return
    is_self_doc = path == (REPO_ROOT / "scripts" / "check_doc_links.py")
    for line_number, line in enumerate(text.splitlines(), start=1):
        for match in DOC_PATH_RE.finditer(line):
            target = REPO_ROOT / match.group(0)
            if not target.exists():
                if is_self_doc:
                    continue
                problems.append(f"{rel(path)}:{line_number}: dangling reference {match.group(0)}")
        if not notes_ok and not is_self_doc:
            for match in NOTES_RE.finditer(line):
                problems.append(f"{rel(path)}:{line_number}: notes.md reference {match.group(0)} (notes moved to doc/)")
        if path.suffix.lower() == ".md":
            for match in MD_LINK_RE.finditer(line):
                if line[: match.start()].count("`") % 2 == 1:
                    continue  # inside inline code, not a link
                target_text = match.group(1)
                if "://" in target_text or target_text.startswith(("#", "mailto:")):
                    continue
                target_text = target_text.split("#", 1)[0]
                if not target_text:
                    continue
                if target_text.startswith("/"):
                    continue
                target = (path.parent / target_text).resolve()
                if not target.exists():
                    problems.append(f"{rel(path)}:{line_number}: broken link {match.group(1)}")


def check_header(path: Path, problems: list[str]) -> None:
    relative = rel(path)
    if any(relative.startswith(prefix) for prefix in HEADER_EXEMPT_DIRS):
        return
    try:
        head = path.read_text(encoding="utf-8", errors="replace").splitlines()[:10]
    except OSError:
        return
    is_plan = relative.startswith("doc/plans/")
    stability = [line for line in head if STABILITY_RE.match(line)]
    status = [line for line in head if STATUS_RE.match(line)]
    if is_plan:
        if len(status) != 1 or stability:
            problems.append(f"{relative}: plan needs exactly one 'Status: proposed | in progress | blocked' line in its first 10 lines")
    else:
        if len(stability) != 1 or status:
            problems.append(f"{relative}: doc needs exactly one 'Stability: stable | mostly stable | experimental' line in its first 10 lines")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--allow-notes", action="store_true", help="do not flag notes.md references (transitional)")
    parser.add_argument("--no-headers", action="store_true", help="skip the Stability/Status header check")
    arguments = parser.parse_args()

    problems: list[str] = []
    for path in iter_scan_files():
        check_file(path, problems, notes_ok=arguments.allow_notes)
    if not arguments.no_headers:
        for path in sorted((REPO_ROOT / "doc").rglob("*.md")):
            check_header(path, problems)

    for problem in problems:
        print(problem)
    print(f"check_doc_links: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
