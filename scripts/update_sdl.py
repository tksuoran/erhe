#!/usr/bin/env python3
"""Sync android-project/ from the erhe branch of the SDL fork and update the pin.

erhe's android-project/ directory is a pure mirror of android-project/ in the
tksuoran/SDL fork at the commit recorded in android-project/SDL_SYNC_COMMIT
(and pinned via ERHE_SDL_GIT_TAG in CMakeLists.txt for CPM). All changes to
android-project/ content are made in the fork's "erhe" branch; updating SDL
means rebasing that branch onto a new upstream ref with normal git tools.

This script assumes the fork checkout is already in the desired state. It:
  1. mirrors android-project/ from the fork commit into the erhe repo
     (copying new/changed files and deleting files no longer in the fork),
  2. rewrites ERHE_SDL_GIT_TAG in CMakeLists.txt, and
  3. rewrites android-project/SDL_SYNC_COMMIT.

The commit must be reachable from the fork's GitHub remote (push the branch
and a snapshot tag first) so CPM can fetch it on any machine. Snapshot tags
also keep previously pinned commits alive across rebase force-pushes.

Usage:
    py -3 scripts/update_sdl.py [--sdl-repo <path>] [--ref <ref>] [--dry-run] [--no-fetch]

See doc/android.md, section "Updating SDL".
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
ANDROID_PREFIX = "android-project/"
STAMP_REL = "android-project/SDL_SYNC_COMMIT"
CMAKELISTS = REPO_ROOT / "CMakeLists.txt"
PIN_PATTERN = re.compile(rb'(set\(ERHE_SDL_GIT_TAG ")[0-9a-fA-F]+("\))')

# Upstream sync of these files can change the required gradle / AGP / JDK
# combination; flag them for manual review (e.g. Gradle 8.12 + AGP do not
# support JDK 25).
REVIEW_PATHS = (
    "android-project/gradle/wrapper/",
    "android-project/build.gradle",
    "android-project/gradle.properties",
)


def run_git(repo: Path, *args: str, binary: bool = False):
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "git -C {} {} failed:\n{}".format(repo, " ".join(args), result.stderr.decode(errors="replace"))
        )
    if binary:
        return result.stdout
    return result.stdout.decode().strip()


def try_git(repo: Path, *args: str):
    try:
        return run_git(repo, *args)
    except RuntimeError:
        return None


def is_probably_text(data: bytes) -> bool:
    return b"\0" not in data[:8192]


def content_equal(existing: bytes, blob: bytes) -> bool:
    """True if files match modulo CR/LF (text only)."""
    if existing == blob:
        return True
    if is_probably_text(existing) and is_probably_text(blob):
        return existing.replace(b"\r\n", b"\n") == blob.replace(b"\r\n", b"\n")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--sdl-repo",
        type=Path,
        default=REPO_ROOT.parent / "sdl",
        help="path to the tksuoran/SDL fork checkout (default: ../sdl)",
    )
    parser.add_argument(
        "--ref",
        default="HEAD",
        help="fork ref to sync from (default: HEAD of the fork checkout)",
    )
    parser.add_argument("--dry-run", action="store_true", help="report actions without writing anything")
    parser.add_argument("--no-fetch", action="store_true", help="skip 'git fetch origin' and the remote reachability check")
    args = parser.parse_args()

    sdl = args.sdl_repo
    if try_git(sdl, "rev-parse", "--is-inside-work-tree") != "true":
        print(f"error: {sdl} is not a git work tree (pass --sdl-repo)", file=sys.stderr)
        return 1

    new_hash = try_git(sdl, "rev-parse", f"{args.ref}^{{commit}}")
    if new_hash is None:
        print(f"error: cannot resolve ref '{args.ref}' in {sdl}", file=sys.stderr)
        return 1

    # Refuse to clobber uncommitted erhe work under android-project/. The
    # stamp file is script-owned and exempt; --dry-run writes nothing and
    # may inspect any state.
    if not args.dry_run:
        dirty = run_git(REPO_ROOT, "status", "--porcelain", "--", "android-project/")
        dirty_lines = [line for line in dirty.splitlines() if line[3:] != STAMP_REL]
        if dirty_lines:
            print("error: android-project/ has uncommitted changes; commit or stash first:", file=sys.stderr)
            print("\n".join(dirty_lines), file=sys.stderr)
            return 1

    # The pinned commit must be fetchable from GitHub by CPM on any machine.
    if not args.no_fetch:
        print("fetching origin in the fork ...")
        run_git(sdl, "fetch", "origin")
        reachable = (
            subprocess.run(
                ["git", "-C", str(sdl), "merge-base", "--is-ancestor", new_hash, "origin/erhe"],
                capture_output=True,
            ).returncode
            == 0
        )
        if not reachable:
            # Fall back to any other remote ref (branch or tag) containing
            # the commit. Local-only tags do not count: they do not make the
            # commit fetchable by CPM on other machines.
            for line in run_git(sdl, "ls-remote", "origin").splitlines():
                tip = line.split("\t", 1)[0]
                contains = subprocess.run(
                    ["git", "-C", str(sdl), "merge-base", "--is-ancestor", new_hash, tip],
                    capture_output=True,
                )
                if contains.returncode == 0:
                    reachable = True
                    break
        if not reachable:
            print(
                f"error: {new_hash} is not reachable from origin/erhe or any tag.\n"
                "Push the erhe branch and a snapshot tag first, e.g.:\n"
                f"    git -C {sdl} tag erhe-snapshot-<date> {new_hash}\n"
                f"    git -C {sdl} push origin erhe erhe-snapshot-<date>",
                file=sys.stderr,
            )
            return 1

    describe = try_git(sdl, "describe", "--tags", new_hash) or new_hash

    # ---- Mirror android-project/ from the fork commit ----------------------
    source_paths = run_git(
        sdl, "-c", "core.quotePath=false", "ls-tree", "-r", "--name-only", new_hash, "--", ANDROID_PREFIX
    ).splitlines()
    source_set = set(source_paths)

    added = []
    updated = []
    deleted = []
    review = []
    for path in sorted(source_paths):
        blob = run_git(sdl, "show", f"{new_hash}:{path}", binary=True)
        dest = REPO_ROOT / path
        if dest.exists():
            if content_equal(dest.read_bytes(), blob):
                continue
            updated.append(path)
        else:
            added.append(path)
        if any(path.startswith(prefix) or path == prefix.rstrip("/") for prefix in REVIEW_PATHS):
            review.append(path)
        if not args.dry_run:
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(blob)

    erhe_paths = run_git(REPO_ROOT, "-c", "core.quotePath=false", "ls-files", "--", ANDROID_PREFIX).splitlines()
    for path in sorted(erhe_paths):
        if path in source_set or path == STAMP_REL:
            continue
        deleted.append(path)
        if not args.dry_run:
            (REPO_ROOT / path).unlink()

    # ---- Rewrite the pin in CMakeLists.txt ---------------------------------
    cmake_bytes = CMAKELISTS.read_bytes()
    pin_matches = PIN_PATTERN.findall(cmake_bytes)
    if len(pin_matches) != 1:
        print(f"error: expected exactly one set(ERHE_SDL_GIT_TAG ...) in CMakeLists.txt, found {len(pin_matches)}", file=sys.stderr)
        return 1
    new_cmake = PIN_PATTERN.sub(rb"\g<1>" + new_hash.encode() + rb"\g<2>", cmake_bytes)
    pin_changed = new_cmake != cmake_bytes
    if pin_changed and not args.dry_run:
        CMAKELISTS.write_bytes(new_cmake)

    # ---- Rewrite the stamp -------------------------------------------------
    stamp_lines = [new_hash, describe]
    merge_base = try_git(sdl, "merge-base", new_hash, "upstream/main")
    if merge_base:
        base_describe = try_git(sdl, "describe", "--tags", merge_base) or merge_base
        stamp_lines.append(f"merge-base upstream/main: {merge_base} ({base_describe})")
    stamp_path = REPO_ROOT / STAMP_REL
    stamp_bytes = ("\n".join(stamp_lines) + "\n").encode()
    stamp_changed = not (stamp_path.exists() and stamp_path.read_bytes().replace(b"\r\n", b"\n") == stamp_bytes)
    if stamp_changed and not args.dry_run:
        stamp_path.write_bytes(stamp_bytes)

    # ---- Report ------------------------------------------------------------
    prefix = "[dry-run] " if args.dry_run else ""
    for path in added:
        print(f"{prefix}add    {path}")
    for path in updated:
        print(f"{prefix}update {path}")
    for path in deleted:
        print(f"{prefix}delete {path}")
    print(f"{prefix}pin    ERHE_SDL_GIT_TAG = {new_hash} ({describe})" + ("" if pin_changed else " [unchanged]"))
    if stamp_changed:
        print(f"{prefix}stamp  {STAMP_REL}")
    if review:
        print()
        print("WARNING: upstream changed gradle build machinery; review JDK / AGP")
        print("compatibility manually before trusting the build:")
        for path in review:
            print(f"    {path}")
    if not (added or updated or deleted or pin_changed or stamp_changed):
        print("mirror already in sync; nothing to do")
    elif not args.dry_run:
        print()
        print("next: re-run the configure script, rebuild the quest APK, and test on device")
    return 0


if __name__ == "__main__":
    sys.exit(main())
