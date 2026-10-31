"""Mali Offline Compiler analytics for a shader variant browser directory
(doc/erhe/graphics.md "Shader variant browser").

Reads <browser-directory>/variants/manifest.json (written by the editor run
with --shader-variant-browser), runs the Arm Mali Offline Compiler (malioc)
on every recorded stage module before and after the SPIRV-Tools optimizer,
keeps each raw JSON report next to the module as
variants/<n>_<stage>[_optimized].<core>.malioc.json, and writes
<browser-directory>/analytics.js, the sidecar index.html and the per-program
pages render when it is present.

The compiler is found through --malioc, the MALIOC environment variable, or
"malioc" on PATH. The default core is Mali-G310, the weakest core that accepts
erhe's modules as the device uses them (doc/erhe/graphics.md "Mali
analytics"); --core may repeat. A module whose report is newer than it and
carries the same core and compiler version is not compiled again unless
--force. A failed compile is a result too: the error report is kept, with
every "Unsupported capability (<n>)" resolved to the SPIR-V capability name,
and a compiler crash is recorded as an error naming the signal.

    py -3 scripts/shader_analytics.py <browser-directory> [--core Mali-G310]
        [--malioc <path>] [--jobs N] [--force] [--check]

--check re-reads the analytics.js it wrote and verifies that every program
and stage of the manifest has a before entry, and an after entry where the
optimized module exists. Exit code 0 when every module produced a report,
1 when the compiler or the manifest is missing or the compiler reports a
command line error.
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

DEFAULT_CORE = "Mali-G310"
SIDECAR_SCHEMA_VERSION = 1

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
    4423: "StorageBuffer16BitAccess",
    4427: "StorageUniform16",
    4437: "MultiView",
    4439: "VulkanMemoryModel",
    4441: "DrawParameters",
    4472: "RayQueryKHR",
    5301: "ShaderNonUniform",
    5302: "RuntimeDescriptorArray",
    5347: "PhysicalStorageBufferAddresses",
    5379: "DemoteToHelperInvocation",
    5391: "RayQueryPositionFetchKHR",
}


def find_malioc(explicit):
    candidates = [explicit, os.environ.get("MALIOC"), shutil.which("malioc")]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    return None


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


def query_compiler(malioc, cores):
    """The compiler version and the validated core list, or exits."""
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


def report_is_current(report_path, module_path, core, version):
    if not report_path.is_file():
        return False
    if report_path.stat().st_mtime < module_path.stat().st_mtime:
        return False
    try:
        with open(report_path, encoding="utf-8") as f:
            report = json.load(f)
    except (OSError, json.JSONDecodeError):
        return False
    erhe = report.get("erhe", {})
    return erhe.get("core") == core and erhe.get("malioc_version") == version


def compile_module(malioc, module_path, report_path, core, version, entry_point, specialization_constants):
    """Runs malioc on one module and writes the raw report with an 'erhe' block."""
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
    temp = report_path.with_suffix(report_path.suffix + ".tmp")
    with open(temp, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    os.replace(temp, report_path)
    return True, None


def summarize_report(report):
    """The sidecar's per-module summary from a raw performance or error report."""
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
        "core": hardware.get("core"),
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


def load_manifest(directory):
    path = directory / "variants" / "manifest.json"
    if not path.is_file():
        sys.exit(f"shader_analytics: {path} not found; record a browser directory with the editor's --shader-variant-browser option first")
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    if manifest.get("schema", {}).get("name") != "erhe_shader_variants":
        sys.exit(f"shader_analytics: {path} is not an erhe shader variant manifest")
    return manifest


def plan_jobs(manifest, variants_dir, cores, version, force):
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
                    if not force and report_is_current(report_path, module_path, core, version):
                        skipped += 1
                        continue
                    jobs.append((module_path, report_path, core, stage.get("entry_point", "main"), stage.get("specialization_constants", [])))
    return jobs, skipped


def build_sidecar(manifest, variants_dir, cores, version, malioc):
    """The analytics.js object: per core, per program id, per stage, before/after summaries."""
    sidecar = {
        "schema": {"name": "erhe_shader_analytics", "version": SIDECAR_SCHEMA_VERSION},
        "generated": datetime.datetime.now().isoformat(timespec="seconds"),
        "malioc": malioc,
        "malioc_version": version,
        "manifest_tools": manifest.get("tools", {}),
        "default_core": cores[0],
        "cores": {},
    }
    missing = 0
    for core in cores:
        core_block = {"core": core, "revision": None, "architecture": None, "driver": None, "pipelines": [], "malioc_version": version, "generated": sidecar["generated"], "programs": {}}
        for program in manifest.get("programs", []):
            program_block = {}
            for stage in program.get("stages", []):
                stage_block = {"before": None, "after": None}
                for key, slot in (("spirv", "before"), ("optimized_spirv", "after")):
                    name = stage.get(key)
                    if not name:
                        continue
                    report_path = variants_dir / f"{Path(name).stem}.{core}.malioc.json"
                    if not report_path.is_file():
                        missing += 1
                        continue
                    with open(report_path, encoding="utf-8") as f:
                        report = json.load(f)
                    summary, core_info = summarize_report(report)
                    stage_block[slot] = summary
                    if core_info and not core_block["pipelines"]:
                        core_block.update({k: v for k, v in core_info.items() if k != "core"})
                program_block[stage.get("type")] = stage_block
            core_block["programs"][str(program.get("id"))] = program_block
        sidecar["cores"][core] = core_block
    return sidecar, missing


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
                if stage.get("optimized_spirv") and stage_block["after"] is None:
                    problems.append(f"{core}: program {program['id']} {stage['type']}: no after entry")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path, help="the shader variant browser directory (holds index.html and variants/)")
    parser.add_argument("--core", action="append", help=f"target Arm GPU (repeatable; default {DEFAULT_CORE})")
    parser.add_argument("--malioc", help="path to the malioc executable (else $MALIOC, else PATH)")
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)), help="parallel compiler processes")
    parser.add_argument("--force", action="store_true", help="compile every module even when its report is current")
    parser.add_argument("--check", action="store_true", help="after writing analytics.js, verify it covers the manifest")
    args = parser.parse_args()

    directory = args.directory.resolve()
    variants_dir = directory / "variants"
    manifest = load_manifest(directory)
    cores = args.core or [DEFAULT_CORE]
    malioc = find_malioc(args.malioc)
    if malioc is None:
        sys.exit("shader_analytics: malioc not found; pass --malioc, set MALIOC, or put it on PATH")
    version = query_compiler(malioc, cores)
    tools = manifest.get("tools", {})
    if tools.get("api") not in (None, "vulkan"):
        print(f"shader_analytics: the manifest's modules were compiled for api '{tools.get('api')}'; they are analyzed as Vulkan modules", file=sys.stderr)

    jobs, skipped = plan_jobs(manifest, variants_dir, cores, version, args.force)
    print(f"shader_analytics: {malioc} {version}; cores {', '.join(cores)}; {len(jobs)} module(s) to compile, {skipped} current")
    failures = []
    if jobs:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(compile_module, malioc, *job[:3], version, *job[3:]): job for job in jobs}
            done = 0
            for future in concurrent.futures.as_completed(futures):
                ok, message = future.result()
                done += 1
                if not ok:
                    failures.append(message)
                if done % 50 == 0 or done == len(jobs):
                    print(f"shader_analytics: {done}/{len(jobs)}")
    for message in failures:
        print(f"shader_analytics: {message}", file=sys.stderr)

    sidecar, missing = build_sidecar(manifest, variants_dir, cores, version, malioc)
    path = write_sidecar(directory, sidecar)
    errors = 0
    modules = 0
    for core_block in sidecar["cores"].values():
        for program_block in core_block["programs"].values():
            for stage_block in program_block.values():
                for slot in ("before", "after"):
                    if stage_block[slot] is not None:
                        modules += 1
                        if stage_block[slot]["error"]:
                            errors += 1
    print(f"shader_analytics: wrote {path}: {len(manifest.get('programs', []))} program(s), {modules} module report(s), {errors} with compile errors, {missing} missing")

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
