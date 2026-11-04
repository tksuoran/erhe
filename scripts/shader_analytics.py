"""Offline GPU compiler analytics for a shader variant browser directory
(doc/erhe/graphics.md "Shader variant browser" and "Mali analytics").

Reads <browser-directory>/variants/manifest.json (written by the editor run
with --shader-variant-browser), runs the vendor offline compilers on every
recorded program before and after the SPIRV-Tools optimizer, keeps each raw
JSON report next to the modules, and writes <browser-directory>/analytics.js,
the sidecar index.html and the per-program pages render when it is present.

The compiler is chosen per core by its name:

- Mali-<name> (Arm Mali Offline Compiler, malioc): found through --malioc,
  the MALIOC environment variable, or "malioc" on PATH. One compile per stage
  module; report variants/<n>_<stage>[_optimized].<core>.malioc.json.

Without --core every compiler found contributes its default core: Mali-G310
(the weakest Mali core that accepts erhe's modules). --core may repeat. A report newer than its module(s) with the same core and compiler
version is not compiled again unless --force. A failed compile is a result
too: the error is kept in the report (malioc: every "Unsupported capability
(<n>)" resolved to the SPIR-V capability name; a compiler crash is recorded as
an error naming the signal).

    py -3 scripts/shader_analytics.py <browser-directory> [--core Mali-G310]
        [--malioc <path>] [--jobs N] [--force] [--check]

--check re-reads the analytics.js it wrote and verifies that every program
and stage of the manifest has a before entry, and an after entry where the
optimized module(s) exist. Exit code 0 when every module produced a report,
1 when a compiler or the manifest is missing or a compiler reports a command
line error.
"""

import argparse
import concurrent.futures
import datetime
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_MALI_CORE = "Mali-G310"
SIDECAR_SCHEMA_VERSION = 2

# SPIR-V capability numbers the error reports name (spirv.hpp); only the
# ones erhe's modules declare and a few neighbours.
CAPABILITY_NAMES = {
    1: "Shader",
    11: "Float16",
    22: "Int64",
    23: "Int16",
    32: "ClipDistance",
    33: "CullDistance",
    45: "SampledCubeArray",
    49: "StorageImageExtendedFormats",
    50: "ImageQuery",
    51: "DerivativeControl",
    61: "GroupNonUniform",
    4423: "SubgroupBallotKHR",
    4427: "DrawParameters",
    4433: "StorageBuffer16BitAccess",
    4434: "UniformAndStorageBuffer16BitAccess",
    4437: "DeviceGroup",
    4439: "MultiView",
    4441: "VariablePointersStorageBuffer",
    4472: "RayQueryKHR",
    5301: "ShaderNonUniform",
    5302: "RuntimeDescriptorArray",
    5345: "VulkanMemoryModel",
    5347: "PhysicalStorageBufferAddresses",
    5379: "DemoteToHelperInvocation",
    5391: "RayQueryPositionFetchKHR",
}


def compiler_of(core):
    if core.startswith("Mali-"):
        return "malioc"
    sys.exit(f"shader_analytics: core {core} names no Mali (Mali-<name>) GPU")


def find_tool(explicit, env_name, exe_name):
    candidates = [explicit, os.environ.get(env_name), shutil.which(exe_name)]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    return None


def write_json_via_temp(path, data):
    temp = path.with_suffix(path.suffix + ".tmp")
    with open(temp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    os.replace(temp, path)


def read_report(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return None


# ---------------------------------------------------------------- Mali (malioc)

def run_json(command):
    """Run malioc with --format json; returns (exit_code, parsed_json_or_None, raw_output)."""
    completed = subprocess.run(command, capture_output=True, text=True)
    output = completed.stdout if completed.stdout.strip() else completed.stderr
    parsed = None
    if output.strip().startswith("{"):
        try:
            parsed = json.loads(output)
        except json.JSONDecodeError:
            parsed = None
    return completed.returncode, parsed, output


def query_malioc(malioc, cores):
    """The compiler version, after validating the cores, or exits."""
    code, listing, raw = run_json([malioc, "--list", "--format", "json"])
    if code != 0 or listing is None:
        sys.exit(f"shader_analytics: {malioc} --list failed ({code}):\n{raw[:500]}")
    producer = listing.get("producer", {})
    version = ".".join(str(x) for x in producer.get("version", []))
    # list schema version 0: {"cores": [{"core": "Mali-G310", "apis": [...]}, ...]}
    known = {entry.get("core") for entry in listing.get("cores", []) if isinstance(entry, dict) and entry.get("core")}
    for core in cores:
        if known and core not in known:
            sys.exit(f"shader_analytics: unknown core {core}; malioc --list names: {', '.join(sorted(known))}")
    return version


def resolve_capability_names(message):
    """'Unsupported capability (5302)' -> 'Unsupported capability (5302 RuntimeDescriptorArray)'."""
    out = message
    start = 0
    while True:
        i = out.find("Unsupported capability (", start)
        if i < 0:
            return out
        j = out.find(")", i)
        if j < 0:
            return out
        number = out[i + len("Unsupported capability (") : j]
        if number.isdigit() and int(number) in CAPABILITY_NAMES:
            out = out[:j] + " " + CAPABILITY_NAMES[int(number)] + out[j:]
        start = j + 1


def malioc_report_is_current(report_path, module_path, core, version):
    if not report_path.is_file():
        return False
    if report_path.stat().st_mtime < module_path.stat().st_mtime:
        return False
    report = read_report(report_path)
    if report is None:
        return False
    erhe = report.get("erhe", {})
    return erhe.get("core") == core and erhe.get("malioc_version") == version


def compile_malioc(malioc, version, job):
    """Runs malioc on one module and writes the raw report with an 'erhe' block."""
    module_path, report_path, core, entry_point, specialization_constants = job
    command = [malioc, "--vulkan", "--spirv", "-c", core, "--format", "json"]
    if entry_point and entry_point != "main":
        command += ["-n", entry_point]
    for constant in specialization_constants:
        command += ["-S", f"{constant['location']}={constant['value']}"]
    command.append(str(module_path))
    try:
        completed = subprocess.run(command, capture_output=True, text=True)
    except OSError as error:
        return False, f"cannot run {malioc}: {error}"
    output = completed.stdout if completed.stdout.strip() else completed.stderr
    report = None
    if output.strip().startswith("{"):
        try:
            report = json.loads(output)
        except json.JSONDecodeError:
            report = None
    if completed.returncode == 2:
        return False, f"malioc command line error for {module_path.name}:\n{output[:800]}"
    if report is None:
        # A crash (negative return code: signal) or non-JSON output.
        if completed.returncode < 0:
            message = f"compiler crashed with signal {-completed.returncode}"
        else:
            message = f"compiler exited {completed.returncode} without a JSON report: {output.strip()[:300]}"
        report = {"schema": {"name": "error", "version": 0}, "shaders": [{"errors": [message]}]}
    report["erhe"] = {
        "core": core,
        "malioc_version": version,
        "module": module_path.name,
        "command": " ".join(command),
        "exit_code": completed.returncode,
    }
    write_json_via_temp(report_path, report)
    return True, None


def summarize_malioc_report(report):
    """The sidecar's per-module summary from a raw malioc performance or error report."""
    erhe = report.get("erhe", {})
    summary = {"command": erhe.get("command", ""), "error": None, "variants": [], "properties": {}, "attribute_streams": None, "warnings": [], "notes": []}
    shaders = report.get("shaders", [])
    if report.get("schema", {}).get("name") != "performance" or not shaders:
        messages = []
        for shader in shaders:
            for error in shader.get("errors", []):
                # error schema version 0: a list of strings
                messages.append(resolve_capability_names(error if isinstance(error, str) else json.dumps(error)))
        summary["error"] = "; ".join(messages) if messages else "no performance report"
        return summary, None
    shader = shaders[0]
    hardware = shader.get("hardware", {})
    pipelines = [p.get("name") for p in hardware.get("pipelines", [])]
    core_info = {
        "revision": hardware.get("revision"),
        "architecture": hardware.get("architecture"),
        "driver": shader.get("driver"),
        "pipelines": pipelines,
    }
    for prop in shader.get("properties", []):
        summary["properties"][prop.get("name")] = prop.get("value")
    for variant in shader.get("variants", []):
        props = {p.get("name"): p.get("value") for p in variant.get("properties", [])}
        performance = variant.get("performance", {})
        # A variant's cycle_count arrays follow the variant's own "pipelines"
        # order, which is not the hardware block's order; the sidecar stores
        # them in the hardware order so one list names every column.
        variant_pipelines = performance.get("pipelines", pipelines)

        def cycles(key):
            entry = performance.get(key, {})
            counts = entry.get("cycle_count")
            if counts is not None and variant_pipelines != pipelines:
                by_name = dict(zip(variant_pipelines, counts))
                counts = [by_name.get(name) for name in pipelines]
            return counts, [b for b in entry.get("bound_pipelines", []) if b]

        total, bound_total = cycles("total_cycles")
        shortest, bound_shortest = cycles("shortest_path_cycles")
        longest, bound_longest = cycles("longest_path_cycles")
        summary["variants"].append({
            "name": variant.get("name"),
            "work_registers": props.get("work_registers_used"),
            "uniform_registers": props.get("uniform_registers_used"),
            "occupancy": props.get("thread_occupancy"),
            "stack_size": props.get("stack_size"),
            "has_spilling": props.get("has_stack_spilling"),
            "spill_bytes": props.get("stack_spill_bytes"),
            "alloca_bytes": props.get("stack_alloca_bytes"),
            "fp16": props.get("fp16_arithmetic"),
            "total": total,
            "bound_total": bound_total,
            "shortest": shortest,
            "bound_shortest": bound_shortest,
            "longest": longest,
            "bound_longest": bound_longest,
        })
    summary["attribute_streams"] = shader.get("attribute_streams")
    summary["warnings"] = [w if isinstance(w, str) else json.dumps(w) for w in shader.get("warnings", [])]
    summary["notes"] = [n if isinstance(n, str) else json.dumps(n) for n in shader.get("notes", [])]
    return summary, core_info


def plan_malioc_jobs(manifest, variants_dir, cores, version, force):
    """(module path, report path, core, entry point, spec constants) for every module to compile."""
    jobs = []
    skipped = 0
    for program in manifest.get("programs", []):
        for stage in program.get("stages", []):
            for key in ("spirv", "optimized_spirv"):
                name = stage.get(key)
                if not name:
                    continue
                module_path = variants_dir / name
                if not module_path.is_file():
                    print(f"shader_analytics: missing module {module_path}", file=sys.stderr)
                    continue
                for core in cores:
                    report_path = variants_dir / f"{Path(name).stem}.{core}.malioc.json"
                    if not force and malioc_report_is_current(report_path, module_path, core, version):
                        skipped += 1
                        continue
                    jobs.append((module_path, report_path, core, stage.get("entry_point", "main"), stage.get("specialization_constants", [])))
    return jobs, skipped


def build_malioc_core_block(manifest, variants_dir, core, version, generated):
    block = {"core": core, "compiler": "malioc", "revision": None, "architecture": None, "driver": None, "pipelines": [], "malioc_version": version, "generated": generated, "programs": {}}
    missing = 0
    for program in manifest.get("programs", []):
        program_block = {}
        for stage in program.get("stages", []):
            stage_block = {"before": None, "after": None}
            for key, slot in (("spirv", "before"), ("optimized_spirv", "after")):
                name = stage.get(key)
                if not name:
                    continue
                report = read_report(variants_dir / f"{Path(name).stem}.{core}.malioc.json")
                if report is None:
                    missing += 1
                    continue
                summary, core_info = summarize_malioc_report(report)
                stage_block[slot] = summary
                if core_info and not block["pipelines"]:
                    block.update(core_info)
            program_block[stage.get("type")] = stage_block
        block["programs"][str(program.get("id"))] = program_block
    return block, missing


# ------------------------------------------------------------------- sidecar

def load_manifest(directory):
    path = directory / "variants" / "manifest.json"
    if not path.is_file():
        sys.exit(f"shader_analytics: {path} not found; record a browser directory with the editor's --shader-variant-browser option first")
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    if manifest.get("schema", {}).get("name") != "erhe_shader_variants":
        sys.exit(f"shader_analytics: {path} is not an erhe shader variant manifest")
    return manifest


def write_sidecar(directory, sidecar):
    path = directory / "analytics.js"
    # '<' escaped so no "</script>" can end the element that loads this file.
    text = "window.erhe_shader_analytics = " + json.dumps(sidecar, indent=1).replace("<", "\\u003c") + ";\n"
    temp = path.with_suffix(".js.tmp")
    with open(temp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(temp, path)
    return path


def check_sidecar(directory, manifest):
    """Re-reads analytics.js as JSON and verifies its coverage of the manifest."""
    path = directory / "analytics.js"
    with open(path, encoding="utf-8") as f:
        text = f.read()
    prefix = "window.erhe_shader_analytics = "
    if not text.startswith(prefix) or not text.rstrip().endswith(";"):
        return [f"{path}: unexpected framing"]
    sidecar = json.loads(text[len(prefix) :].rstrip().rstrip(";"))
    problems = []
    for core, core_block in sidecar["cores"].items():
        for program in manifest.get("programs", []):
            program_block = core_block["programs"].get(str(program["id"]))
            if program_block is None:
                problems.append(f"{core}: program {program['id']} missing")
                continue
            for stage in program.get("stages", []):
                stage_block = program_block.get(stage["type"])
                if stage_block is None:
                    problems.append(f"{core}: program {program['id']} stage {stage['type']} missing")
                    continue
                if stage_block["before"] is None:
                    problems.append(f"{core}: program {program['id']} {stage['type']}: no before entry")
                expect_after = bool(stage.get("optimized_spirv"))
                if expect_after and stage_block["after"] is None:
                    problems.append(f"{core}: program {program['id']} {stage['type']}: no after entry")
    return problems


def run_jobs(label, function, jobs, worker_count):
    failures = []
    if not jobs:
        return failures
    with concurrent.futures.ThreadPoolExecutor(max_workers=worker_count) as pool:
        futures = [pool.submit(function, job) for job in jobs]
        done = 0
        for future in concurrent.futures.as_completed(futures):
            ok, message = future.result()
            done += 1
            if not ok:
                failures.append(message)
            if done % 50 == 0 or done == len(jobs):
                print(f"shader_analytics: {label} {done}/{len(jobs)}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path, help="the shader variant browser directory (holds index.html and variants/)")
    parser.add_argument("--core", action="append", help=f"target GPU, Mali-<name> (repeatable; default {DEFAULT_MALI_CORE} when malioc is found)")
    parser.add_argument("--malioc", help="path to the malioc executable (else $MALIOC, else PATH)")
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)), help="parallel compiler processes")
    parser.add_argument("--force", action="store_true", help="compile every module even when its report is current")
    parser.add_argument("--check", action="store_true", help="after writing analytics.js, verify it covers the manifest")
    args = parser.parse_args()

    directory = args.directory.resolve()
    variants_dir = directory / "variants"
    manifest = load_manifest(directory)
    malioc = find_tool(args.malioc, "MALIOC", "malioc")
    if args.core:
        cores = list(dict.fromkeys(args.core))
        if any(compiler_of(c) == "malioc" for c in cores) and malioc is None:
            sys.exit("shader_analytics: malioc not found; pass --malioc, set MALIOC, or put it on PATH")
    else:
        cores = [DEFAULT_MALI_CORE] if malioc else []
        if not cores:
            sys.exit("shader_analytics: malioc not found; pass --malioc, set MALIOC, or put it on PATH")
    mali_cores = [c for c in cores if compiler_of(c) == "malioc"]

    tools = manifest.get("tools", {})
    if tools.get("api") not in (None, "vulkan"):
        print(f"shader_analytics: the manifest's modules were compiled for api '{tools.get('api')}'; they are analyzed as Vulkan modules", file=sys.stderr)

    generated = datetime.datetime.now().isoformat(timespec="seconds")
    sidecar = {
        "schema": {"name": "erhe_shader_analytics", "version": SIDECAR_SCHEMA_VERSION},
        "generated": generated,
        "manifest_tools": tools,
        "default_core": cores[0],
        "default_cores": {},
        "cores": {},
    }
    failures = []
    missing = 0

    if mali_cores:
        malioc_version = query_malioc(malioc, mali_cores)
        jobs, skipped = plan_malioc_jobs(manifest, variants_dir, mali_cores, malioc_version, args.force)
        print(f"shader_analytics: {malioc} {malioc_version}; cores {', '.join(mali_cores)}; {len(jobs)} module(s) to compile, {skipped} current")
        failures += run_jobs("malioc", lambda job: compile_malioc(malioc, malioc_version, job), jobs, args.jobs)
        sidecar.update({"malioc": malioc, "malioc_version": malioc_version})
        sidecar["default_cores"]["malioc"] = mali_cores[0]
        for core in mali_cores:
            block, core_missing = build_malioc_core_block(manifest, variants_dir, core, malioc_version, generated)
            sidecar["cores"][core] = block
            missing += core_missing

    for message in failures:
        print(f"shader_analytics: {message}", file=sys.stderr)

    path = write_sidecar(directory, sidecar)
    for core, core_block in sidecar["cores"].items():
        entries = 0
        errors = 0
        for program_block in core_block["programs"].values():
            for stage_block in program_block.values():
                for slot in ("before", "after"):
                    if stage_block[slot] is not None:
                        entries += 1
                        if stage_block[slot]["error"]:
                            errors += 1
        print(f"shader_analytics: {core}: {entries} stage report(s), {errors} with compile errors")
    print(f"shader_analytics: wrote {path}: {len(manifest.get('programs', []))} program(s), {missing} report(s) missing")

    if args.check:
        problems = check_sidecar(directory, manifest)
        for problem in problems:
            print(f"shader_analytics: check: {problem}", file=sys.stderr)
        if problems:
            return 1
        print("shader_analytics: check passed")
    return 1 if (failures or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
