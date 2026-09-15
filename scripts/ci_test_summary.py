#!/usr/bin/env python3
"""Summarize the JUnit files that `ctest --output-junit` wrote in CI.

Used by .github/workflows/tests.yml: every build job of build.yml runs ctest
with --output-junit and uploads the XML as an artifact; the tests workflow
downloads them all into one directory and runs this script on it. The
script prints a per-configuration table (to stdout, and to the job summary
when GITHUB_STEP_SUMMARY is set), lists every failed test with its output,
and exits non-zero when any test failed, when a result file is malformed, or
when the directory holds no result files at all (a configuration whose
build failed uploads nothing, and no results must never read as green).

Usage: ci_test_summary.py <results_dir>
"""
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


class Suite:
    def __init__(self, name: str) -> None:
        self.name = name
        self.tests = 0
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.time_s = 0.0
        self.failures: list[tuple[str, str]] = []


def parse_junit(path: Path) -> Suite:
    suite = Suite(path.stem)
    root = ET.parse(path).getroot()
    for case in root.iter("testcase"):
        suite.tests += 1
        suite.time_s += float(case.get("time", "0") or 0)
        name = case.get("name", "?")
        failure = case.find("failure")
        if failure is None:
            failure = case.find("error")
        if failure is not None:
            suite.failed += 1
            system_out = case.find("system-out")
            output = (system_out.text or "") if system_out is not None else ""
            message = failure.get("message", "") or ""
            suite.failures.append((name, (message + "\n" + output).strip()))
        elif (case.find("skipped") is not None) or (case.get("status", "run") != "run"):
            # ctest writes status="disabled" for gtest DISABLED_ cases and
            # status="notrun" for tests it could not start; neither passed.
            suite.skipped += 1
        else:
            suite.passed += 1
    return suite


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    results_dir = Path(argv[1])
    files = sorted(results_dir.rglob("*.xml"))
    lines: list[str] = []
    exit_code = 0
    if not files:
        lines.append(f"No test result files under `{results_dir}`: at least one build job produced no results.")
        exit_code = 1
    suites: list[Suite] = []
    for path in files:
        try:
            suites.append(parse_junit(path))
        except ET.ParseError as error:
            lines.append(f"Malformed result file `{path}`: {error}")
            exit_code = 1

    lines.append("| Configuration | Tests | Passed | Failed | Not run | Time |")
    lines.append("| :--- | ---: | ---: | ---: | ---: | ---: |")
    for suite in suites:
        marker = "FAIL" if suite.failed else "ok"
        lines.append(
            f"| {suite.name} ({marker}) | {suite.tests} | {suite.passed} | {suite.failed} "
            f"| {suite.skipped} | {suite.time_s:.0f} s |"
        )
        if suite.failed:
            exit_code = 1
    for suite in suites:
        for name, output in suite.failures:
            lines.append("")
            lines.append(f"### {suite.name}: {name}")
            lines.append("```")
            lines.append(output[-4000:])
            lines.append("```")

    text = "\n".join(lines) + "\n"
    print(text)
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as summary:
            summary.write(text)
    return exit_code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
