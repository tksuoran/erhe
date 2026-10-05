#!/usr/bin/env python3
"""Run the CI ctest invocation and tee its output to a file, line by line.

ctest's own --output-log is written when ctest exits, so a run that is cut
off (a step timeout, a dead runner) leaves that file empty. This wrapper
streams ctest's stdout to the console and to <results_dir>/<build_dir>.log,
flushing every line, so the file names the test that was running when the
run was cut off. Exits with ctest's exit code.

Usage: ci_run_tests.py [--label <regex>] <build_dir> <config> <results_dir> [<name>]

<name> is the stem of the two output files (default: <build_dir>), for a
matrix entry that builds in the same directory as another one, such as the
sanitizer entry.

Without --label the run leaves out the tests labeled "gpu" (they bring up a
graphics Device) and "editor" (they drive a running editor): the default for
a runner without a GPU. --label runs only the tests whose label matches the
regex instead, which is how the GPU test job runs "gpu" on a software Vulkan.
"""

import pathlib
import subprocess
import sys


def main(argv: list[str]) -> int:
    label = None
    if (len(argv) >= 3) and (argv[1] == "--label"):
        label = argv[2]
        argv = [argv[0]] + argv[3:]
    if len(argv) not in (4, 5):
        print(__doc__, file=sys.stderr)
        return 2
    build_dir = argv[1]
    config = argv[2]
    results_dir = pathlib.Path(argv[3])
    name = argv[4] if len(argv) == 5 else build_dir
    results_dir.mkdir(parents=True, exist_ok=True)
    log_path = results_dir / f"{name}.log"
    junit_path = results_dir / f"{name}.xml"

    command = [
        "ctest",
        "--test-dir", build_dir,
        "--build-config", config,
        *(["--label-regex", label] if label is not None else ["--label-exclude", "gpu|editor"]),
        "--output-on-failure",
        "--timeout", "120",
        "--output-junit", str(junit_path),
    ]
    print(" ".join(command), flush=True)
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            log.write(line)
            log.flush()
        return process.wait()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
